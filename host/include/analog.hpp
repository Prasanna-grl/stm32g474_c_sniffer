#pragma once

#include "record.hpp"

#include <optional>
#include <string>
#include <vector>

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

struct SbuChunk {
  uint64_t index = 0;
  uint32_t timestamp_us = 0;
  uint8_t line = 0;
  uint8_t valid_count = 0;
  std::vector<uint8_t> data;
};

std::optional<AnalogSnapshot> parse_analog_record(uint64_t index,
                                                  const RawRecord &record);
std::optional<SbuChunk> parse_sbu_chunk(uint64_t index,
                                        const RawRecord &record);
std::string format_time_us(uint64_t timestamp_us);
std::string format_analog(const AnalogSnapshot &snapshot);
std::string analog_csv_header();
std::string analog_csv_row(const AnalogSnapshot &snapshot);
std::string sbu_chunk_csv_header();
std::string sbu_chunk_csv_row(const SbuChunk &chunk);

} // namespace g474
