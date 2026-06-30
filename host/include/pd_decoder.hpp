#pragma once

#include "record.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace g474 {

struct DecodeConfig {
  uint32_t sample_clock_hz = 24'000'000;
  uint16_t half_min = 30;
  uint16_t half_max = 50;
  uint16_t full_min = 70;
  uint16_t full_max = 100;
  uint16_t max_gap = 400;
  bool include_overflow = false;
  bool include_crc_bad = false;
};

struct PdPacket {
  uint8_t channel = 0;
  uint8_t bit_offset = 0;
  uint32_t symbol_index = 0;
  uint64_t record_index = 0;
  uint32_t timestamp_us = 0;
  std::string ordered_set;
  std::vector<uint8_t> payload;
  std::vector<uint8_t> symbols;
  bool crc_ok = false;
};

struct DecodeIssue {
  uint8_t channel = 0;
  uint8_t bit_offset = 0;
  uint32_t symbol_index = 0;
  uint64_t record_index = 0;
  uint32_t timestamp_us = 0;
  std::string ordered_set;
  std::string reason;
  std::vector<uint8_t> symbols;
};

struct StreamSummary {
  uint32_t edge_records = 0;
  uint32_t overflow_records = 0;
  uint32_t decoded_bits = 0;
  uint32_t chunk_order_warnings = 0;
};

struct DecodeResult {
  StreamSummary streams[2];
  std::vector<PdPacket> packets;
  std::vector<DecodeIssue> issues;
};

class PdDecoder {
public:
  explicit PdDecoder(DecodeConfig config = {});

  void add_record(uint64_t index, const RawRecord &record);
  DecodeResult decode() const;

private:
  DecodeConfig config_;
  std::vector<EdgeRecord> streams_[2];
};

std::string message_name(const std::vector<uint8_t> &payload);
std::string header_summary(const std::vector<uint8_t> &payload);
std::string format_packet(const PdPacket &packet);
std::string format_decode_issue(const DecodeIssue &issue);

} // namespace g474
