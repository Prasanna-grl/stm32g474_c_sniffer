#pragma once

#include <cstdint>
#include <set>
#include <vector>

struct libusb_context;
struct libusb_device_handle;

namespace g474 {

class UsbDevice {
public:
  UsbDevice(uint16_t vid, uint16_t pid, int interface_number);
  ~UsbDevice();

  UsbDevice(const UsbDevice &) = delete;
  UsbDevice &operator=(const UsbDevice &) = delete;

  std::vector<uint8_t> bulk_read(uint8_t endpoint, int read_size,
                                 unsigned timeout_ms);
  void bulk_write(uint8_t endpoint, const std::vector<uint8_t> &data,
                  unsigned timeout_ms);

private:
  libusb_context *ctx_ = nullptr;
  libusb_device_handle *handle_ = nullptr;
  int interface_number_ = 0;
  std::set<int> claimed_interfaces_;
};

} // namespace g474
