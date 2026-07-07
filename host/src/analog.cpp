#include "analog.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace g474 {

namespace {

constexpr std::size_t kSbuSourceOffset = 22;
constexpr std::size_t kSbuCountOffset = 23;
constexpr std::size_t kSbuPayloadOffset = 24;
constexpr std::size_t kSbuPayloadMax = 40;

} // namespace

static int16_t read_le16s(const uint8_t *p) {
  return static_cast<int16_t>(read_le16(p));
}

std::optional<AnalogSnapshot> parse_analog_record(uint64_t index,
                                                  const RawRecord &record) {
  const uint16_t code = read_le16(record.data());
  if (code != kAnalogCodeWord && code != 0xA5A5u) {
    return std::nullopt;
  }

  AnalogSnapshot snapshot;
  snapshot.index = index;
  if (code == kAnalogCodeWord) {
    snapshot.format = "f0";
    snapshot.timestamp_us = read_le32(record.data() + 2);
    snapshot.vbus_mv = read_le16(record.data() + 6);
    snapshot.vbus_ma = read_le16s(record.data() + 8);
    snapshot.cc1_mv = read_le16(record.data() + 10);
    snapshot.cc1_ma = read_le16s(record.data() + 12);
    snapshot.cc2_mv = read_le16(record.data() + 14);
    snapshot.cc2_ma = read_le16s(record.data() + 16);
  } else {
    snapshot.format = "legacy_a5a5";
    snapshot.timestamp_us = read_le16(record.data() + 2);
    snapshot.vbus_mv = read_le16(record.data() + 4);
    snapshot.vbus_ma = read_le16s(record.data() + 6);
    snapshot.cc1_mv = read_le16(record.data() + 8);
    snapshot.cc1_ma = read_le16s(record.data() + 10);
    snapshot.cc2_mv = read_le16(record.data() + 12);
    snapshot.cc2_ma = read_le16s(record.data() + 14);
  }
  return snapshot;
}

std::optional<SbuChunk> parse_sbu_chunk(uint64_t index,
                                        const RawRecord &record) {
  const uint8_t source = record[kSbuSourceOffset];
  if (source != 1u && source != 2u) {
    return std::nullopt;
  }

  const uint8_t count = record[kSbuCountOffset];
  if (count == 0u) {
    return std::nullopt;
  }

  SbuChunk chunk;
  chunk.index = index;
  chunk.timestamp_us = read_le32(record.data() + 2);
  chunk.line = source;
  chunk.valid_count =
      static_cast<uint8_t>(std::min<std::size_t>(count, kSbuPayloadMax));
  chunk.data.assign(record.begin() + kSbuPayloadOffset,
                    record.begin() + kSbuPayloadOffset + chunk.valid_count);
  return chunk;
}

std::string format_time_us(uint64_t timestamp_us) {
  const uint64_t hours = timestamp_us / 3'600'000'000ull;
  timestamp_us %= 3'600'000'000ull;
  const uint64_t minutes = timestamp_us / 60'000'000ull;
  timestamp_us %= 60'000'000ull;
  const uint64_t seconds = timestamp_us / 1'000'000ull;
  timestamp_us %= 1'000'000ull;
  const uint64_t millis = timestamp_us / 1000ull;
  const uint64_t micros = timestamp_us % 1000ull;

  std::ostringstream out;
  out << std::setfill('0');
  if (hours != 0) {
    out << hours << "h ";
  }
  if (hours != 0 || minutes != 0) {
    out << minutes << "min ";
  }
  out << seconds << "s " << std::setw(3) << millis << "ms " << std::setw(3)
      << micros << "us";
  return out.str();
}

std::string format_analog(const AnalogSnapshot &snapshot) {
  std::ostringstream out;
  out << "ANA rec=" << snapshot.index << " format=" << snapshot.format
      << " ts=" << format_time_us(snapshot.timestamp_us)
      << " ts_us=" << snapshot.timestamp_us << " vbus=" << snapshot.vbus_mv
      << "mV/" << snapshot.vbus_ma << "mA"
      << " cc1=" << snapshot.cc1_mv << "mV/" << snapshot.cc1_ma << "mA"
      << " cc2=" << snapshot.cc2_mv << "mV/" << snapshot.cc2_ma << "mA";
  return out.str();
}

std::string analog_csv_header() {
  return "index,format,timestamp_us,time,vbus_mv,vbus_ma,cc1_mv,cc1_ma,cc2_mv,cc2_ma\n";
}

std::string analog_csv_row(const AnalogSnapshot &snapshot) {
  std::ostringstream out;
  out << snapshot.index << ',' << snapshot.format << ',' << snapshot.timestamp_us
      << ',' << format_time_us(snapshot.timestamp_us) << ','
      << snapshot.vbus_mv << ',' << snapshot.vbus_ma << ','
      << snapshot.cc1_mv << ',' << snapshot.cc1_ma << ','
      << snapshot.cc2_mv << ',' << snapshot.cc2_ma << '\n';
  return out.str();
}

std::string sbu_chunk_csv_header() {
  return "index,timestamp_us,time,line,valid_count,raw_hex\n";
}

std::string sbu_chunk_csv_row(const SbuChunk &chunk) {
  std::ostringstream out;
  out << chunk.index << ',' << chunk.timestamp_us << ','
      << format_time_us(chunk.timestamp_us) << ",SBU"
      << static_cast<unsigned>(chunk.line) << ','
      << static_cast<unsigned>(chunk.valid_count) << ','
      << hex_bytes(chunk.data) << '\n';
  return out.str();
}

} // namespace g474
