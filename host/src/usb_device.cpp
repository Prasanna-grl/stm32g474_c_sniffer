#include "usb_device.hpp"

#include <libusb-1.0/libusb.h>

#include <stdexcept>
#include <string>

namespace g474 {

static std::string usb_error(int code) {
  return std::string(libusb_error_name(code));
}

UsbDevice::UsbDevice(uint16_t vid, uint16_t pid, int interface_number)
    : interface_number_(interface_number) {
  int rc = libusb_init(&ctx_);
  if (rc != 0) {
    throw std::runtime_error("libusb_init failed: " + usb_error(rc));
  }

  handle_ = libusb_open_device_with_vid_pid(ctx_, vid, pid);
  if (!handle_) {
    throw std::runtime_error("USB device not found");
  }

  rc = libusb_set_auto_detach_kernel_driver(handle_, 1);
  (void)rc;

  rc = libusb_claim_interface(handle_, interface_number_);
  if (rc != 0) {
    throw std::runtime_error("libusb_claim_interface failed: " + usb_error(rc));
  }
  claimed_ = true;
}

UsbDevice::~UsbDevice() {
  if (handle_ && claimed_) {
    libusb_release_interface(handle_, interface_number_);
  }
  if (handle_) {
    libusb_close(handle_);
  }
  if (ctx_) {
    libusb_exit(ctx_);
  }
}

std::vector<uint8_t> UsbDevice::bulk_read(uint8_t endpoint, int read_size,
                                          unsigned timeout_ms) {
  std::vector<uint8_t> buffer(static_cast<std::size_t>(read_size));
  int transferred = 0;
  const int rc = libusb_bulk_transfer(handle_, endpoint, buffer.data(),
                                      read_size, &transferred, timeout_ms);
  if (rc == LIBUSB_ERROR_TIMEOUT) {
    return {};
  }
  if (rc != 0) {
    throw std::runtime_error("libusb_bulk_transfer failed: " + usb_error(rc));
  }
  buffer.resize(static_cast<std::size_t>(transferred));
  return buffer;
}

} // namespace g474
