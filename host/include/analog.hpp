#pragma once

#include "record.hpp"

#include <optional>
#include <string>

namespace g474 {

struct AnalogSnapshot {
  uint64_t index = 0;
  std::string format;
  uint32_t timestamp_us = 0;
  uint16_t vbus_mv = 0;
  int16_t vbus_ma = 0;
  uint16_t cc1_mv = 0;
  int16_t cc1_ma = 0;
  uint16_t cc2_mv = 0;
  int16_t cc2_ma = 0;
};

std::optional<AnalogSnapshot> parse_analog_record(uint64_t index,
                                                  const RawRecord &record);
std::string format_time_us(uint64_t timestamp_us);
std::string format_analog(const AnalogSnapshot &snapshot);
std::string analog_csv_header();
std::string analog_csv_row(const AnalogSnapshot &snapshot);

} // namespace g474
