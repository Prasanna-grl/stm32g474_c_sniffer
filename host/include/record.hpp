#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace g474 {

constexpr std::size_t kRecordSize = 64;
constexpr std::size_t kEdgePayloadOffset = 8;
constexpr std::size_t kEdgeSampleCount = 28;
constexpr uint16_t kAnalogCodeWord = 0x2001;
constexpr uint8_t kEdgeMarker = 0x13;

using RawRecord = std::array<uint8_t, kRecordSize>;

enum class RecordKind {
  Short,
  Edge,
  Analog,
  Unknown,
};

struct EdgeRecord {
  uint64_t index = 0;
  uint8_t channel = 0;
  uint8_t seq_page = 0;
  bool overflow = false;
  uint32_t timestamp_us = 0;
  uint8_t chunk = 0;
  std::array<uint16_t, kEdgeSampleCount> samples{};
};

uint16_t read_le16(const uint8_t *p);
uint32_t read_le32(const uint8_t *p);
std::string hex_bytes(const std::vector<uint8_t> &data);
std::string hex_bytes(const uint8_t *data, std::size_t len);

RecordKind classify_record(const RawRecord &record);
std::optional<EdgeRecord> parse_edge_record(uint64_t index,
                                            const RawRecord &record);
std::vector<RawRecord> records_from_bytes(const std::vector<uint8_t> &bytes);

} // namespace g474
