from __future__ import print_function

import os
import sys
import tempfile
import textwrap
import time
import unittest

try:
    from urllib import urlretrieve
except ImportError:
    from urllib.request import urlretrieve

try:
    from unittest import mock
except ImportError:
    import mock

from pysmu import Device, Session, DeviceError

# input = raw_input in py3, copy this for py2
if sys.hexversion < 0x03000000:
    input = raw_input


def prompt(s):
    """Prompt the user to verify test setup before continuing."""
    input('ACTION: {} (hit Enter to continue)'.format(s))


class TestDevice(unittest.TestCase):

    def setUp(self):
        self.session = Session()
        self.session.scan()
        self.assertTrue(len(self.session.available_devices))
        self.device = self.session.available_devices[0]

    def tearDown(self):
        del self.session

    def test_device_serial(self):
        prompt('make sure at least one device is plugged in')
        self.assertTrue(self.device.serial)

    def test_device_fwver(self):
        self.assertTrue(self.device.fwver)

    def test_device_hwver(self):
        self.assertTrue(self.device.hwver)

    def test_calibration(self):
        self.assertEqual(len(self.device.calibration), 8)

    def test_write_calibration(self):
        if (float(self.device.fwver) < 2.06):
            self.skipTest('writing calibration is only supported by firmware >= 2.06')

        default_cal = [
            [0.0, 1.0, 1.0],
            [0.0, 1.0, 1.0],
            [0.0, 1.0, 1.0],
            [0.0, 1.0, 1.0],
            [0.0, 1.0, 1.0],
            [0.0, 1.0, 1.0],
            [0.0, 1.0, 1.0],
            [0.0, 1.0, 1.0],
        ]

        # reset calibration
        self.device.write_calibration(None)
        self.assertEqual(self.device.calibration, default_cal)

        # writing nonexistent calibration file
        with self.assertRaises(DeviceError):
            self.device.write_calibration('nonexistent')

        # writing bad calibration file
        cal_data = tempfile.NamedTemporaryFile()
        with open(cal_data.name, 'w') as f:
            f.write('foo')
        with self.assertRaises(DeviceError):
            self.device.write_calibration(cal_data.name)

        # writing good calibration file
        dn = os.path.dirname
        cal_data = os.path.join(dn(dn(dn(dn(__file__)))), 'contrib', 'calib.txt')
        self.device.write_calibration(cal_data)
        self.assertEqual(self.device.calibration, default_cal)

    def test_samba_mode(self):
        # assumes an internet connection is available and github is up
        fw_url = 'https://github.com/analogdevicesinc/m1k-fw/releases/download/v2.06/m1000.bin'

        # fetch old/new firmware files from github
        fw = tempfile.NamedTemporaryFile()
        urlretrieve(fw_url, fw.name)

        # supported devices exist in the session
        num_available_devices = len(self.session.available_devices)
        self.assertTrue(num_available_devices)
        orig_serial = self.session.available_devices[0].serial

        # pushing one into SAM-BA mode drops it from the session after rescan
        self.device.samba_mode()
        self.session.scan()
        self.assertEqual(len(self.session.available_devices), num_available_devices - 1)
        self.assertFalse(any(d.serial == orig_serial for d in self.session.available_devices))

        # flash device in SAM-BA mode
        self.session.flash_firmware(fw.name)
        prompt('unplug/replug the device')
        self.session.scan()

        # device is re-added after hotplug
        self.assertEqual(len(self.session.available_devices), num_available_devices)
        self.device = self.session.available_devices[0]
        self.assertEqual(self.device.serial, orig_serial)
        self.assertEqual(self.device.fwver, '2.06')
