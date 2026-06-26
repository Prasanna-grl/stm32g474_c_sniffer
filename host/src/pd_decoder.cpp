#include "pd_decoder.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace g474 {
namespace {

constexpr uint8_t SYNC1 = 0x18;
constexpr uint8_t SYNC2 = 0x11;
constexpr uint8_t SYNC3 = 0x06;
constexpr uint8_t RST1 = 0x07;
constexpr uint8_t RST2 = 0x19;
constexpr uint8_t EOP = 0x0D;

const std::array<uint8_t, 4> SOP = {SYNC1, SYNC1, SYNC1, SYNC2};
const std::array<uint8_t, 4> SOP_PRIME = {SYNC1, SYNC1, SYNC3, SYNC3};
const std::array<uint8_t, 4> SOP_DPRIME = {SYNC1, SYNC3, SYNC1, SYNC3};

const std::map<uint8_t, uint8_t> FIVEB_TO_NIBBLE = {
    {0x1E, 0x0}, {0x09, 0x1}, {0x14, 0x2}, {0x15, 0x3},
    {0x0A, 0x4}, {0x0B, 0x5}, {0x0E, 0x6}, {0x0F, 0x7},
    {0x12, 0x8}, {0x13, 0x9}, {0x16, 0xA}, {0x17, 0xB},
    {0x1A, 0xC}, {0x1B, 0xD}, {0x1C, 0xE}, {0x1D, 0xF},
};

const std::map<uint8_t, const char *> CONTROL_MESSAGE_TYPES = {
    {0x01, "GoodCRC"},
    {0x02, "GotoMin"},
    {0x03, "Accept"},
    {0x04, "Reject"},
    {0x05, "Ping"},
    {0x06, "PS_RDY"},
    {0x07, "Get_Source_Cap"},
    {0x08, "Get_Sink_Cap"},
    {0x09, "DR_Swap"},
    {0x0A, "PR_Swap"},
    {0x0B, "VCONN_Swap"},
    {0x0C, "Wait"},
    {0x0D, "Soft_Reset"},
    {0x10, "Not_Supported"},
    {0x11, "Get_Source_Cap_Extended"},
    {0x12, "Get_Status"},
    {0x13, "FR_Swap"},
    {0x14, "Get_PPS_Status"},
    {0x15, "Get_Country_Codes"},
    {0x16, "Get_Sink_Cap_Extended"},
};

const std::map<uint8_t, const char *> DATA_MESSAGE_TYPES = {
    {0x01, "Source_Capabilities"},
    {0x02, "Request"},
    {0x03, "BIST"},
    {0x04, "Sink_Capabilities"},
    {0x05, "Battery_Status"},
    {0x06, "Alert"},
    {0x07, "Get_Country_Info"},
    {0x0F, "Vendor_Defined"},
};

uint32_t crc32(const std::vector<uint8_t> &data, std::size_t len) {
  static uint32_t table[256] = {};
  static bool table_ready = false;
  if (!table_ready) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int j = 0; j < 8; ++j) {
        c = (c & 1u) ? (0xEDB88320u ^ (c >> 1u)) : (c >> 1u);
      }
      table[i] = c;
    }
    table_ready = true;
  }

  uint32_t c = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < len; ++i) {
    c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8u);
  }
  return c ^ 0xFFFFFFFFu;
}

bool crc_ok(const std::vector<uint8_t> &payload) {
  if (payload.size() < 6) {
    return false;
  }
  const std::size_t crc_off = payload.size() - 4u;
  const uint32_t expected = read_le32(payload.data() + crc_off);
  const uint32_t actual = crc32(payload, crc_off);
  return actual == expected;
}

std::string ordered_set_name(const std::vector<uint8_t> &symbols,
                             std::size_t index) {
  if (index + 4u > symbols.size()) {
    return {};
  }
  const std::array<uint8_t, 4> set = {symbols[index], symbols[index + 1],
                                      symbols[index + 2], symbols[index + 3]};
  if (set == SOP) {
    return "SOP";
  }
  if (set == SOP_PRIME) {
    return "SOP'";
  }
  if (set == SOP_DPRIME) {
    return "SOP\"";
  }
  if (set == std::array<uint8_t, 4>{RST1, RST1, RST1, RST2}) {
    return "Hard_Reset";
  }
  if (set == std::array<uint8_t, 4>{RST1, RST1, RST1, RST1}) {
    return "Cable_Reset";
  }
  return {};
}

std::vector<uint8_t> decode_payload_symbols(const std::vector<uint8_t> &symbols,
                                            std::size_t begin,
                                            std::size_t end,
                                            bool *ok) {
  std::vector<uint8_t> nibbles;
  for (std::size_t i = begin; i < end; ++i) {
    const auto it = FIVEB_TO_NIBBLE.find(symbols[i]);
    if (it == FIVEB_TO_NIBBLE.end()) {
      *ok = false;
      return {};
    }
    nibbles.push_back(it->second);
  }

  if (nibbles.size() % 2u) {
    *ok = false;
    return {};
  }

  std::vector<uint8_t> payload;
  payload.reserve(nibbles.size() / 2u);
  for (std::size_t i = 0; i < nibbles.size(); i += 2u) {
    payload.push_back(static_cast<uint8_t>(nibbles[i] | (nibbles[i + 1] << 4u)));
  }
  *ok = true;
  return payload;
}

void samples_for_stream(const std::vector<EdgeRecord> &records,
                        std::vector<uint16_t> &samples,
                        std::vector<uint64_t> &owners,
                        std::vector<uint32_t> &timestamps) {
  for (const auto &record : records) {
    for (uint16_t sample : record.samples) {
      samples.push_back(sample);
      owners.push_back(record.index);
      timestamps.push_back(record.timestamp_us);
    }
  }
}

void drop_consecutive_duplicates(std::vector<uint16_t> &samples,
                                 std::vector<uint64_t> &owners,
                                 std::vector<uint32_t> &timestamps) {
  if (samples.empty()) {
    return;
  }

  std::vector<uint16_t> filtered_samples;
  std::vector<uint64_t> filtered_owners;
  std::vector<uint32_t> filtered_timestamps;
  filtered_samples.push_back(samples[0]);
  filtered_owners.push_back(owners[0]);
  filtered_timestamps.push_back(timestamps[0]);

  for (std::size_t i = 1; i < samples.size(); ++i) {
    if (samples[i] == filtered_samples.back()) {
      continue;
    }
    filtered_samples.push_back(samples[i]);
    filtered_owners.push_back(owners[i]);
    filtered_timestamps.push_back(timestamps[i]);
  }

  samples.swap(filtered_samples);
  owners.swap(filtered_owners);
  timestamps.swap(filtered_timestamps);
}

void decode_edges_to_bits(const std::vector<uint16_t> &samples,
                          const std::vector<uint64_t> &owners,
                          const std::vector<uint32_t> &timestamps,
                          const DecodeConfig &config,
                          std::vector<uint8_t> &bits,
                          std::vector<uint64_t> &bit_owners,
                          std::vector<uint32_t> &bit_timestamps) {
  bool have_pending_half = false;
  uint64_t pending_half_owner = 0;
  uint32_t pending_half_timestamp = 0;

  for (std::size_t i = 1; i < samples.size(); ++i) {
    const uint16_t delta = static_cast<uint16_t>(samples[i] - samples[i - 1]);

    if (delta > config.max_gap) {
      have_pending_half = false;
      continue;
    }

    if (config.half_min <= delta && delta <= config.half_max) {
      if (!have_pending_half) {
        pending_half_owner = owners[i];
        pending_half_timestamp = timestamps[i];
        have_pending_half = true;
      } else {
        bits.push_back(1);
        bit_owners.push_back(pending_half_owner);
        bit_timestamps.push_back(pending_half_timestamp);
        have_pending_half = false;
      }
      continue;
    }

    if (config.full_min <= delta && delta <= config.full_max) {
      have_pending_half = false;
      bits.push_back(0);
      bit_owners.push_back(owners[i]);
      bit_timestamps.push_back(timestamps[i]);
      continue;
    }

    have_pending_half = false;
  }
}

void bits_to_symbols(const std::vector<uint8_t> &bits,
                     const std::vector<uint64_t> &owners,
                     const std::vector<uint32_t> &timestamps, std::size_t offset,
                     std::vector<uint8_t> &symbols,
                     std::vector<uint64_t> &symbol_owners,
                     std::vector<uint32_t> &symbol_timestamps) {
  for (std::size_t pos = offset; pos + 4u < bits.size(); pos += 5u) {
    uint8_t value = 0;
    for (std::size_t bit = 0; bit < 5u; ++bit) {
      value |= static_cast<uint8_t>(bits[pos + bit] << bit);
    }
    symbols.push_back(value);
    symbol_owners.push_back(owners[pos]);
    symbol_timestamps.push_back(timestamps[pos]);
  }
}

std::vector<PdPacket> find_packets(uint8_t channel,
                                   const std::vector<uint8_t> &bits,
                                   const std::vector<uint64_t> &owners,
                                   const std::vector<uint32_t> &timestamps,
                                   bool include_crc_bad) {
  std::vector<PdPacket> packets;

  for (std::size_t offset = 0; offset < 5u; ++offset) {
    std::vector<uint8_t> symbols;
    std::vector<uint64_t> symbol_owners;
    std::vector<uint32_t> symbol_timestamps;
    bits_to_symbols(bits, owners, timestamps, offset, symbols, symbol_owners,
                    symbol_timestamps);

    std::size_t index = 0;
    while (index + 4u < symbols.size()) {
      const std::string name = ordered_set_name(symbols, index);
      if (name.empty()) {
        ++index;
        continue;
      }

      if (name == "Hard_Reset" || name == "Cable_Reset") {
        PdPacket packet;
        packet.channel = channel;
        packet.bit_offset = static_cast<uint8_t>(offset);
        packet.symbol_index = static_cast<uint32_t>(index);
        packet.record_index = symbol_owners[index];
        packet.timestamp_us = symbol_timestamps[index];
        packet.ordered_set = name;
        packet.symbols.assign(symbols.begin() + index, symbols.begin() + index + 4);
        packet.crc_ok = true;
        packets.push_back(packet);
        index += 4u;
        continue;
      }

      std::size_t eop_index = symbols.size();
      const std::size_t scan_end = std::min(index + 104u, symbols.size());
      for (std::size_t scan = index + 4u; scan < scan_end; ++scan) {
        if (symbols[scan] == EOP) {
          eop_index = scan;
          break;
        }
      }

      if (eop_index == symbols.size()) {
        ++index;
        continue;
      }

      bool payload_ok = false;
      std::vector<uint8_t> payload =
          decode_payload_symbols(symbols, index + 4u, eop_index, &payload_ok);
      if (payload_ok) {
        const bool ok = crc_ok(payload);
        if (ok || include_crc_bad) {
          PdPacket packet;
          packet.channel = channel;
          packet.bit_offset = static_cast<uint8_t>(offset);
          packet.symbol_index = static_cast<uint32_t>(index);
          packet.record_index = symbol_owners[index];
          packet.timestamp_us = symbol_timestamps[index];
          packet.ordered_set = name;
          packet.payload = std::move(payload);
          packet.symbols.assign(symbols.begin() + index,
                                symbols.begin() + eop_index + 1u);
          packet.crc_ok = ok;
          packets.push_back(std::move(packet));
        }
      }

      index = eop_index + 1u;
    }
  }

  return packets;
}

} // namespace

PdDecoder::PdDecoder(DecodeConfig config) : config_(config) {}

void PdDecoder::add_record(uint64_t index, const RawRecord &record) {
  auto edge = parse_edge_record(index, record);
  if (!edge) {
    return;
  }
  if (edge->overflow && !config_.include_overflow) {
    return;
  }
  streams_[edge->channel].push_back(*edge);
}

DecodeResult PdDecoder::decode() const {
  DecodeResult result;

  for (uint8_t channel = 0; channel < 2; ++channel) {
    const auto &records = streams_[channel];
    auto &summary = result.streams[channel];
    summary.edge_records = static_cast<uint32_t>(records.size());

    const EdgeRecord *previous = nullptr;
    for (const auto &record : records) {
      if (record.overflow) {
        ++summary.overflow_records;
      }
      if (previous) {
        const uint8_t expected = static_cast<uint8_t>((previous->chunk + 1u) & 0x07u);
        if (record.chunk != expected && record.chunk != 0) {
          ++summary.chunk_order_warnings;
        }
      }
      previous = &record;
    }

    std::vector<uint16_t> samples;
    std::vector<uint64_t> owners;
    std::vector<uint32_t> timestamps;
    samples_for_stream(records, samples, owners, timestamps);
    drop_consecutive_duplicates(samples, owners, timestamps);

    std::vector<uint8_t> bits;
    std::vector<uint64_t> bit_owners;
    std::vector<uint32_t> bit_timestamps;
    decode_edges_to_bits(samples, owners, timestamps, config_, bits, bit_owners,
                         bit_timestamps);
    summary.decoded_bits = static_cast<uint32_t>(bits.size());

    auto packets = find_packets(channel, bits, bit_owners, bit_timestamps,
                                config_.include_crc_bad);
    result.packets.insert(result.packets.end(), packets.begin(), packets.end());
  }

  std::sort(result.packets.begin(), result.packets.end(),
            [](const PdPacket &a, const PdPacket &b) {
              if (a.record_index != b.record_index) {
                return a.record_index < b.record_index;
              }
              return a.channel < b.channel;
            });

  return result;
}

std::string message_name(const std::vector<uint8_t> &payload) {
  if (payload.size() < 2u) {
    return "Reset";
  }
  const uint16_t header = read_le16(payload.data());
  const uint8_t type = static_cast<uint8_t>(header & 0x1Fu);
  const uint8_t object_count = static_cast<uint8_t>((header >> 12u) & 0x07u);

  const auto &table = object_count ? DATA_MESSAGE_TYPES : CONTROL_MESSAGE_TYPES;
  const auto it = table.find(type);
  if (it != table.end()) {
    return it->second;
  }

  std::ostringstream out;
  out << (object_count ? "Data_0x" : "Control_0x") << std::hex << std::setw(2)
      << std::setfill('0') << static_cast<unsigned>(type);
  return out.str();
}

std::string header_summary(const std::vector<uint8_t> &payload) {
  if (payload.size() < 2u) {
    return "";
  }
  const uint16_t header = read_le16(payload.data());
  std::ostringstream out;
  out << std::hex << std::setfill('0') << "hdr=0x" << std::setw(4) << header
      << std::dec << " type=0x" << std::hex << std::setw(2)
      << (header & 0x1Fu) << std::dec << " rev=" << ((header >> 6u) & 0x03u)
      << " id=" << ((header >> 9u) & 0x07u)
      << " objs=" << ((header >> 12u) & 0x07u)
      << " pr=" << ((header >> 8u) & 0x01u)
      << " dr=" << ((header >> 5u) & 0x01u)
      << " ext=" << ((header >> 15u) & 0x01u);
  return out.str();
}

std::string format_packet(const PdPacket &packet) {
  std::ostringstream out;
  out << "CC" << static_cast<unsigned>(packet.channel + 1u)
      << " rec=" << packet.record_index << " ts_us=" << packet.timestamp_us
      << " offset=" << static_cast<unsigned>(packet.bit_offset)
      << " sym=" << packet.symbol_index << ' ' << packet.ordered_set << ' '
      << (packet.crc_ok ? "CRC_OK" : "CRC_BAD") << ' '
      << message_name(packet.payload) << ' ' << header_summary(packet.payload)
      << " bytes=" << hex_bytes(packet.payload);
  return out.str();
}

} // namespace g474
