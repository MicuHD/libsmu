// Released under the terms of the BSD License
// (C) 2014-2016
//   Analog Devices, Inc.
//   Kevin Mehall <km@kevinmehall.net>
//   Ian Daniher <itdaniher@gmail.com>

/// @file libsmu.hpp
/// @brief Main public interface.

#pragma once

#include <cstdint>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include <libusb.h>

#include <libsmu/version.hpp>

/// @brief List of supported devices.
/// The list uses the vendor and project IDs from USB
/// information formatted as {vendor_id, product_id}.
const std::vector<std::vector<uint16_t>> SUPPORTED_DEVICES = {
	{0x0456, 0xcee2}, // old
	{0x064b, 0x784c}, // new
};

/// @brief List of supported devices in SAM-BA bootloader mode.
/// The list uses the vendor and project IDs from USB information
/// formatted as {vendor_id, product_id}.
const std::vector<std::vector<uint16_t>> SAMBA_DEVICES = {
	{0x03eb, 0x6124}, // shows up as a CDC device by default
};

/// @brief Signal information.
typedef struct sl_signal_info {
	/// Signal label.
	const char* label;

	/// Bitmask of modes for which this signal is enabled as input.
	uint32_t inputModes;

	/// Bitmask of modes for which this signal is enabled as output.
	uint32_t outputModes;

	/// Minimum possible value for the signal.
	double min;

	/// Maximum possible value for the signal.
	double max;

	/// Signal resolution.
	double resolution;
} sl_signal_info;

/// @brief Channel info.
typedef struct sl_channel_info {
	const char* label; ///< Channel label.
	size_t mode_count; ///< Number of available modes.
	size_t signal_count; ///< Number of available signals.
} sl_channel_info;

/// @brief Device info.
typedef struct sl_device_info {
	const char* label; ///< Device label.
	size_t channel_count; ///< Number of available channels.
} sl_device_info;

/// @brief Supported signal sources.
enum Src {
	SRC_CONSTANT, ///< Constant value output.
	SRC_SQUARE, ///< Square wave output.
	SRC_SAWTOOTH, ///< Sawtooth wave output.
	SRC_STAIRSTEP, ///< Stairstep wave output.
	SRC_SINE, ///< Sine wave output.
	SRC_TRIANGLE, ///< Triangle wave output.
	SRC_BUFFER, ///< Use samples from a specified buffer.
	SRC_CALLBACK, ///< Use samples from a specified callback function.
};

/// @brief Supported signal destinations.
enum Dest {
	DEST_DEFAULT, ///< Samples are pushed into a FIFO buffer.
	DEST_BUFFER, ///< Samples are buffered into a specified location.
	DEST_CALLBACK, ///< Samples are passed to a specified callback function.
};

/// @brief Supported channel modes.
enum Modes {
	DISABLED, ///< Channel is disabled.
	SVMI, ///< Source voltage, measure current.
	SIMV, ///< Source current, measure voltage.
};

namespace smu {
	class Device;
	class Signal;

	/// @brief Generic session class.
	class Session {
	public:
		Session();
		~Session();

		/// @brief Devices that are present on the system.
		/// Note that these devices consist of all supported devices currently
		/// recognized on the system; however, the devices aren't necessarily
		/// bound to a session. In order to add devices to a session, add()
		/// must be used.
		std::vector<std::shared_ptr<Device>> m_available_devices;

		/// @brief Devices that are part of this session.
		/// These devices will be started when start() is called.
		/// Use `add()` and `remove()` to manipulate this set.
		std::set<Device*> m_devices;

		/// @brief Number of devices currently streaming samples.
		unsigned m_active_devices;

		/// @brief Size of input/output sample queues for every device.
		/// Alter this if necessary to make continuous data flow work for the
		/// target usage. The default is approximately 100ms worth of samples.
		unsigned m_queue_size = 10000;

		/// @brief Scan system for all supported devices.
		/// Updates the list of available, supported devices for the session
		/// (m_available_devices).
		/// @return On success, 0 is returned.
		/// @return On error, a negative errno code is returned.
		int scan();

		/// @brief Add a device to the session.
		/// This method may not be called while the session is active.
		/// @param device The device to be added to the session.
		/// @return On success, the added device is returned.
		/// @return On error, NULL is returned.
		Device* add(Device* device);

		/// @brief Shim to scan and add all available devices to a session.
		/// This method may not be called while the session is active.
		/// @return On success, 0 is returned.
		/// @return On error, the number of devices that couldn't be added to
		/// the session are returned.
		int add_all();

		/// @brief Get the device matching a given serial from the session.
		/// @param serial A string of a device's serial number.
		/// @return On success, the matching device is returned.
		/// @return If no match is found, NULL is returned.
		Device* get_device(const char* serial);

		/// @brief Remove a device from the session.
		/// @param device A device to be removed from the session.
		/// This method may not be called while the session is active.
		void remove(Device* device);

		/// @brief Remove a device from the list of available devices.
		/// @param device A device to be removed from the available list.
		/// Devices are automatically added to this list on attach.
		/// Devices must be removed from this list on detach.
		/// This method may not be called while the session is active.
		void destroy(Device* device);

		/// @brief Configure the session's sample rate.
		/// @param sampleRate The requested sample rate for the session.
		/// @return On success, 0 is returned.
		/// @return On error, a negative errno code is returned.
		/// This method may not be called while the session is active.
		int configure(uint64_t sampleRate);

		/// @brief Run the currently configured capture and wait for it to complete.
		/// @param samples Number of samples to capture until we stop. If 0, run in continuous mode.
		void run(uint64_t samples);

		/// @brief Start the currently configured capture, but do not wait for it to complete.
		/// @param samples Number of samples to capture until we stop. If 0, run in continuous mode.
		/// Once started, the only allowed Session methods are cancel() and end()
		/// until the session has stopped.
		void start(uint64_t samples);

		/// @brief Cancel capture and block waiting for it to complete.
		void cancel();

		/// @brief Determine the cancellation status of a session.
		/// @return True, if the session has been cancelled (usually from
		/// explicitly calling cancel() or cancelled USB transactions).
		/// @return False, if the session hasn't been started, is running, or
		/// has been stopped successfully.
		bool cancelled() { return m_cancellation != 0; }

		/// @brief Update device firmware for a given device.
		/// @param file Firmware file started for deployment to the device.
		/// @param device The Device targeted for updating.
		/// If device is NULL the first attached device in a session will be
		/// used instead. If no configured devices are found, devices in SAM-BA
		/// bootloader mode are searched for and the first matching device is used.
		/// @throws std::runtime_error for various USB failures causing aborted flashes.
		void flash_firmware(const char *file, Device* device = NULL);

		/// internal: Called by devices on the USB thread when they are complete.
		void completion();
		/// internal: Called by devices on the USB thread when a device encounters an error.
		void handle_error(int status, const char * tag);
		/// internal: Called by device attach events on the USB thread.
		void attached(libusb_device* usb_dev);
		/// internal: Called by device detach events on the USB thread.
		void detached(libusb_device* usb_dev);

		/// @brief Block until all devices have are finished streaming in the session.
		void wait_for_completion();

		/// @brief Block until all devices have completed, then turn off the devices.
		void end();

		/// @brief Callback run via the USB thread on session completion.
		/// Called with the current value of m_cancellation as an argument,
		/// i.e. if the parameter is non-zero we are waiting to complete a
		/// cancelled session.
		std::function<void(unsigned)> m_completion_callback;

		/// @brief Callback called on the USB thread when a device is plugged into the system.
		std::function<void(Device* device)> m_hotplug_detach_callback;

		/// @brief Callback called on the USB thread when a device is removed from the system.
		std::function<void(Device* device)> m_hotplug_attach_callback;

	protected:
		/// @brief Flag used to cancel all pending USB transactions for devices in a session.
		unsigned m_cancellation = 0;

		/// @brief Flag for controlling USB event handling.
		/// USB event handling loop will be run while m_usb_thread_loop is true.
		std::atomic<bool> m_usb_thread_loop;
		/// @brief USB thread handling pending events in blocking mode.
		std::thread m_usb_thread;

		/// @brief Lock for session completion.
		std::mutex m_lock;
		/// @brief Lock for the available device list.
		/// All code that references m_available_devices needs to acquire this lock
		/// before accessing it.
		std::mutex m_lock_devlist;
		/// @brief Blocks on m_lock until session completion is finished.
		std::condition_variable m_completion;

		/// @brief libusb context related with a session.
		/// This allows for segregating libusb usage so external users can
		/// also use libusb without interfering with internal usage.
		libusb_context* m_usb_ctx;

		/// @brief Identify devices supported by libsmu.
		/// @param usb_dev libusb device
		/// @return If the usb device relates to a supported device the Device is returned,
		/// otherwise NULL is returned.
		std::shared_ptr<Device> probe_device(libusb_device* usb_dev);

		/// @brief Find an existing, available device.
		/// @param usb_dev libusb device
		/// @return If the usb device relates to an existing,
		/// available device the Device is returned,
		/// otherwise NULL is returned.
		std::shared_ptr<Device> find_existing_device(libusb_device* usb_dev);
	};

	/// @brief Generic device class.
	class Device {
	public:
		virtual ~Device();

		/// @brief Get the descriptor for the device.
		virtual const sl_device_info* info() const = 0;

		/// @brief Get the descriptor for the specified channel.
		/// @param channel An unsigned integer relating to the requested channel.
		virtual const sl_channel_info* channel_info(unsigned channel) const = 0;

		/// @brief Get the specified signal.
		/// @param channel An unsigned integer relating to the requested channel.
		/// @param signal An unsigned integer relating to the requested signal.
		/// @return The related Signal.
		virtual Signal* signal(unsigned channel, unsigned signal) = 0;

		/// @brief Get the serial number of the device.
		virtual const char* serial() const { return m_serial; }

		/// @brief Get the firmware version of the device.
		virtual const char* fwver() const { return m_fw_version; }

		/// @brief Get the hardware version of the device.
		virtual const char* hwver() const { return m_hw_version; }

		/// @brief Set the mode of the specified channel.
		/// @param channel An unsigned integer relating to the requested channel.
		/// @param mode An unsigned integer relating to the requested mode.
		/// @return On success, 0 is returned.
		/// @return On error, a negative integer is returned relating to the error status.
		/// This method may not be called while the session is active.
		virtual int set_mode(unsigned channel, unsigned mode) = 0;

		/// @brief Get all signal samples from a device.
		/// @param buf Buffer object to store sample values into.
		/// @param samples Number of samples to read.
		/// @param timeout Amount of time in milliseconds to wait for samples
		/// to be available. If 0 (the default), return immediately.
		/// @return On success, the number of samples read.
		/// @return On error, a negative integer is returned relating to the error status.
		/// @throws std::system_error of EBUSY if sample overflows have occurred.
		virtual ssize_t read(std::vector<std::array<float, 4>>& buf, size_t samples, unsigned timeout = 0) = 0;

		/// @brief Write data to a specified channel of the device.
		/// @param buf Buffer of samples to write to the specified channel.
		/// @param channel Channel to write samples to.
		/// @param timeout Amount of time in milliseconds to wait for samples
		/// to be written. If 0 (the default), return immediately.
		/// @return On success, the number of samples written.
		/// @return On error, a negative integer is returned relating to the error status.
		/// @throws std::system_error of EBUSY if sample underflows have occurred.
		virtual ssize_t write(std::deque<float>& buf, unsigned channel, unsigned timeout = 0) = 0;

		/// @brief Perform a raw USB control transfer on the underlying USB device.
		/// @return Passes through the return value of the underlying libusb_control_transfer method.
		/// See the libusb_control_transfer() docs for parameter descriptions.
		int ctrl_transfer(unsigned bmRequestType, unsigned bRequest, unsigned wValue, unsigned wIndex,
						unsigned char *data, unsigned wLength, unsigned timeout);

		/// @brief Force the device into SAM-BA bootloader mode.
		/// @return On success, 0 is returned.
		/// @return On error, a negative errno code is returned.
		virtual int samba_mode() = 0;

		/// @brief Get the default sample rate.
		virtual int get_default_rate() { return 100000; }

		/// @brief Prepare multi-device synchronization.
		/// Get current microframe index, set m_sof_start to be time in the future.
		/// @return On success, 0 is returned.
		/// @return On error, a negative errno code is returned.
		virtual int sync() = 0;

		/// @brief Lock the device's mutex.
		/// This prevents this device's transfers from being processed. Hold
		/// only briefly, while modifying signal state.
		virtual void lock() { m_state.lock(); }

		/// @brief Unlock the device's mutex.
		/// Allows this device's transfers to be processed.
		virtual void unlock() { m_state.unlock(); }

		/// @brief Write the device calibration data into the EEPROM.
		/// @param cal_file_name The path to a properly-formatted calibration
		/// data file to write to the device. If NULL is passed, calibration
		/// is reset to the default setting.
		/// @return On success, 0 is returned.
		/// @return On error, a negative integer is returned relating to the error status.
		virtual int write_calibration(const char* cal_file_name) { return 0; }

		/// @brief Get the device calibration data from the EEPROM.
		/// @param cal A vector of float values.
		virtual void calibration(std::vector<std::vector<float>>* cal) = 0;

	protected:
		/// @brief Device constructor.
		Device(Session* s, libusb_device* usb_dev);

		/// @brief Device claiming and initialization when a session adds this device.
		/// @return On success, 0 is returned.
		/// @return On error, a negative errno code is returned.
		virtual int added() { return 0; }

		/// @brief Device releasing when a session removes this device.
		/// @return On success, 0 is returned.
		/// @return On error, a negative errno code is returned.
		virtual int removed() { return 0; }

		/// @brief Configurization and initialization for device sampling.
		/// @param sampleRate The requested sampling rate for the device.
		/// @return On success, 0 is returned.
		/// @return On error, a negative errno code is returned.
		virtual int configure(uint64_t sampleRate) = 0;

		/// @brief Turn on power supplies and clear sampling state.
		/// @return On success, 0 is returned.
		/// @return On error, a negative errno code is returned.
		virtual int on() = 0;

		/// @brief Stop sampling and put outputs into high impedance mode.
		/// @return On success, 0 is returned.
		/// @return On error, a negative errno code is returned.
		virtual int off() = 0;

		/// @brief Make the device start sampling.
		/// @param samples Number of samples to run before stopping.
		/// @return On success, 0 is returned.
		/// @return On error, a negative errno code is returned.
		virtual int run(uint64_t samples) = 0;

		/// @brief Cancel all pending libusb transactions.
		/// @return On success, 0 is returned.
		/// @return On error, a negative errno code is returned.
		virtual int cancel() = 0;

		/// @brief Session this device is associated with.
		Session* const m_session;

		/// @brief Underlying libusb device.
		libusb_device* const m_device = NULL;
		/// @brief Underlying libusb device handle.
		libusb_device_handle* m_usb = NULL;

		/// Cumulative sample number being handled for input.
		uint64_t m_requested_sampleno = 0;
		/// Current sample number being handled for input.
		uint64_t m_in_sampleno = 0;
		/// Current sample number being submitted for output.
		uint64_t m_out_sampleno = 0;

		/// Lock for transfer state.
		std::mutex m_state;

		/// firmware version
		char m_fw_version[32];
		/// hardware version
		char m_hw_version[32];
		/// serial number
		char m_serial[32];

		friend class Session;
	};

	/// @brief Generic signal class.
	class Signal {
	public:
		/// internal: Do not call the constructor directly; obtain a Signal from a Device.
		Signal(const sl_signal_info* info):
			m_info(info)
			{}

		/// @brief Get the descriptor struct of the Signal.
		/// Pointed-to memory is valid for the lifetime of the Device.
		const sl_signal_info* info() const { return m_info; }
		/// Signal information.
		const sl_signal_info* const m_info;
	};
}
