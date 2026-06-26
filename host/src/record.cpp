#include "record.hpp"

#include <iomanip>
#include <sstream>

namespace g474 {

uint16_t read_le16(const uint8_t *p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t read_le32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

std::string hex_bytes(const uint8_t *data, std::size_t len) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (std::size_t i = 0; i < len; ++i) {
    if (i) {
      out << ' ';
    }
    out << std::setw(2) << static_cast<unsigned>(data[i]);
  }
  return out.str();
}

std::string hex_bytes(const std::vector<uint8_t> &data) {
  return hex_bytes(data.data(), data.size());
}

RecordKind classify_record(const RawRecord &record) {
  if (read_le16(record.data()) == kAnalogCodeWord) {
    return RecordKind::Analog;
  }
  if (record[6] == kEdgeMarker) {
    return RecordKind::Edge;
  }
  return RecordKind::Unknown;
}

std::optional<EdgeRecord> parse_edge_record(uint64_t index,
                                            const RawRecord &record) {
  if (classify_record(record) != RecordKind::Edge) {
    return std::nullopt;
  }

  const uint16_t seq = read_le16(record.data());
  EdgeRecord edge;
  edge.index = index;
  edge.channel = (seq & 0x4000u) ? 1u : 0u;
  edge.seq_page = static_cast<uint8_t>((seq & 0x0FF0u) >> 4);
  edge.overflow = (seq & 0x8000u) != 0;
  edge.timestamp_us = read_le32(record.data() + 2);
  edge.chunk = static_cast<uint8_t>(seq & 0x07u);

  for (std::size_t i = 0; i < kEdgeSampleCount; ++i) {
    const std::size_t off = kEdgePayloadOffset + (i * 2u);
    edge.samples[i] = read_le16(record.data() + off);
  }

  return edge;
}

std::vector<RawRecord> records_from_bytes(const std::vector<uint8_t> &bytes) {
  std::vector<RawRecord> records;
  const std::size_t full_len = bytes.size() - (bytes.size() % kRecordSize);
  records.reserve(full_len / kRecordSize);

  for (std::size_t off = 0; off < full_len; off += kRecordSize) {
    RawRecord record{};
    for (std::size_t i = 0; i < kRecordSize; ++i) {
      record[i] = bytes[off + i];
    }
    records.push_back(record);
  }

  return records;
}

} // namespace g474
