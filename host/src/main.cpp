#include "analog.hpp"
#include "pd_decoder.hpp"
#include "record.hpp"
#include "usb_device.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t g_stop = 0;
std::mutex g_print_mutex;

void signal_handler(int) { g_stop = 1; }

enum class StreamMode {
  Pd,
  Analog,
  All,
};

struct Args {
  uint16_t vid = 0x227F;
  uint16_t pid = 0x0009;
  int interface_number = 0;
  uint8_t endpoint = 0x81;
  uint8_t analog_endpoint = 0x82;
  int read_size = 512;
  unsigned timeout_ms = 1000;
  uint64_t max_records = 4096;
  std::string in_bin;
  std::string in_ana_bin;
  std::string out_raw;
  std::string out_ana_raw;
  std::string out_ana_csv;
  std::string out_events_csv;
  StreamMode mode = StreamMode::Pd;
  bool include_overflow = false;
  bool show_crc_bad = false;
};

uint32_t parse_int(const std::string &text) {
  std::size_t pos = 0;
  uint32_t value = static_cast<uint32_t>(std::stoul(text, &pos, 0));
  if (pos != text.size()) {
    throw std::runtime_error("Invalid integer: " + text);
  }
  return value;
}

void usage(const char *argv0) {
  std::cout
      << "Usage:\n"
      << "  " << argv0 << " --in-bin capture_ep1.bin [--show-crc-bad]\n"
      << "  " << argv0 << " --in-ana-bin capture_ep2.bin [--out-csv timeline.csv]\n"
      << "  " << argv0 << " --in-bin capture_ep1.bin --in-ana-bin capture_ep2.bin --out-csv timeline.csv\n"
      << "  " << argv0 << " [--pd|--ana|--all] [--records 4096]\n"
      << "      [--out-raw capture_ep1.bin] [--read-size 512]\n\n"
      << "Options:\n"
      << "  --in-bin PATH          Decode existing raw EP1 binary capture\n"
      << "  --in-ana-bin PATH      Decode existing raw EP2 analog binary capture\n"
      << "  --pd                   Live-read EP1 PD stream only (default)\n"
      << "  --ana                  Live-read EP2 analog stream only\n"
      << "  --all                  Live-read EP1 PD and EP2 analog streams\n"
      << "  --out-raw PATH         Save raw live EP1 PD bytes\n"
      << "  --out-ana-raw PATH     Save raw live EP2 analog bytes\n"
      << "  --out-ana-csv PATH     Save decoded analog CSV\n"
      << "  --out-csv PATH         Save combined PD/analog timeline CSV\n"
      << "  --out-events-csv PATH  Alias for --out-csv\n"
      << "  --vid VALUE            USB VID, default 0x227f\n"
      << "  --pid VALUE            USB PID, default 0x0009\n"
      << "  --interface VALUE      USB interface, default 0\n"
      << "  --endpoint VALUE       USB IN endpoint, default 0x81\n"
      << "  --ana-endpoint VALUE   Analog IN endpoint, default 0x82\n"
      << "  --read-size VALUE      USB read size, default 512\n"
      << "  --timeout-ms VALUE     USB read timeout, default 1000\n"
      << "  --records VALUE        Max live records, default 4096\n"
      << "  --include-overflow     Decode records with firmware overflow flag\n"
      << "  --show-crc-bad         Print packets with bad CRC too\n";
}

Args parse_args(int argc, char **argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto need_value = [&](const char *name) -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error(std::string("Missing value for ") + name);
      }
      return argv[++i];
    };

    if (arg == "--help" || arg == "-h") {
      usage(argv[0]);
      std::exit(0);
    } else if (arg == "--in-bin") {
      args.in_bin = need_value("--in-bin");
    } else if (arg == "--in-ana-bin") {
      args.in_ana_bin = need_value("--in-ana-bin");
    } else if (arg == "--out-raw") {
      args.out_raw = need_value("--out-raw");
    } else if (arg == "--out-ana-raw") {
      args.out_ana_raw = need_value("--out-ana-raw");
    } else if (arg == "--out-ana-csv") {
      args.out_ana_csv = need_value("--out-ana-csv");
    } else if (arg == "--out-csv") {
      args.out_events_csv = need_value("--out-csv");
    } else if (arg == "--out-events-csv") {
      args.out_events_csv = need_value("--out-events-csv");
    } else if (arg == "--pd") {
      args.mode = StreamMode::Pd;
    } else if (arg == "--ana") {
      args.mode = StreamMode::Analog;
    } else if (arg == "--all") {
      args.mode = StreamMode::All;
    } else if (arg == "--vid") {
      args.vid = static_cast<uint16_t>(parse_int(need_value("--vid")));
    } else if (arg == "--pid") {
      args.pid = static_cast<uint16_t>(parse_int(need_value("--pid")));
    } else if (arg == "--interface") {
      args.interface_number = static_cast<int>(parse_int(need_value("--interface")));
    } else if (arg == "--endpoint") {
      args.endpoint = static_cast<uint8_t>(parse_int(need_value("--endpoint")));
    } else if (arg == "--ana-endpoint") {
      args.analog_endpoint =
          static_cast<uint8_t>(parse_int(need_value("--ana-endpoint")));
    } else if (arg == "--read-size") {
      args.read_size = static_cast<int>(parse_int(need_value("--read-size")));
    } else if (arg == "--timeout-ms") {
      args.timeout_ms = parse_int(need_value("--timeout-ms"));
    } else if (arg == "--records") {
      args.max_records = parse_int(need_value("--records"));
    } else if (arg == "--include-overflow") {
      args.include_overflow = true;
    } else if (arg == "--show-crc-bad") {
      args.show_crc_bad = true;
    } else {
      throw std::runtime_error("Unknown option: " + arg);
    }
  }
  return args;
}

std::vector<uint8_t> read_file(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("Unable to open input: " + path);
  }
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                              std::istreambuf_iterator<char>());
}

std::string csv_escape(const std::string &value) {
  bool quote = false;
  for (char c : value) {
    if (c == ',' || c == '"' || c == '\n' || c == '\r') {
      quote = true;
      break;
    }
  }
  if (!quote) {
    return value;
  }
  std::string out = "\"";
  for (char c : value) {
    if (c == '"') {
      out += "\"\"";
    } else {
      out += c;
    }
  }
  out += '"';
  return out;
}

std::string events_csv_header() {
  return "timestamp_us,timestamp,event,cc,record,message,crc_ok,summary,payload_hex,"
         "analog_format,vbus_mv,vbus_ma,cc1_mv,cc1_ma,cc2_mv,cc2_ma\n";
}

std::string pd_event_csv_row(const g474::PdPacket &packet) {
  std::ostringstream out;
  out << packet.timestamp_us << ',' << g474::format_time_us(packet.timestamp_us)
      << ",pd,CC" << static_cast<unsigned>(packet.channel + 1u) << ','
      << packet.record_index << ','
      << csv_escape(packet.ordered_set + " " + g474::message_name(packet.payload))
      << ',' << (packet.crc_ok ? 1 : 0) << ','
      << csv_escape(g474::header_summary(packet.payload)) << ','
      << csv_escape(g474::hex_bytes(packet.payload))
      << ",,,,,,,\n";
  return out.str();
}

std::string analog_event_csv_row(const g474::AnalogSnapshot &snapshot) {
  std::ostringstream out;
  out << snapshot.timestamp_us << ',' << g474::format_time_us(snapshot.timestamp_us)
      << ",analog,," << snapshot.index
      << ",,,,," << snapshot.format << ',' << snapshot.vbus_mv << ','
      << snapshot.vbus_ma << ','
      << snapshot.cc1_mv << ',' << snapshot.cc1_ma << ',' << snapshot.cc2_mv
      << ',' << snapshot.cc2_ma << '\n';
  return out.str();
}

void print_summary(const g474::DecodeResult &result) {
  const auto &cc1 = result.streams[0];
  const auto &cc2 = result.streams[1];
  const char *active_cc = "unknown";
  if (cc1.decoded_bits || cc2.decoded_bits) {
    active_cc = (cc1.decoded_bits >= cc2.decoded_bits) ? "CC1" : "CC2";
  } else if (cc1.edge_records || cc2.edge_records) {
    active_cc = (cc1.edge_records >= cc2.edge_records) ? "CC1" : "CC2";
  }
  std::cout << "active_cc=" << active_cc << " source=edge_stream\n";

  for (int ch = 0; ch < 2; ++ch) {
    const auto &s = result.streams[ch];
    std::cout << "CC" << (ch + 1) << ": edge_records=" << s.edge_records
              << " overflow_records=" << s.overflow_records
              << " decoded_bits=" << s.decoded_bits
              << " chunk_order_warnings=" << s.chunk_order_warnings << '\n';
  }

  uint32_t crc_ok = 0;
  for (const auto &packet : result.packets) {
    if (packet.crc_ok) {
      ++crc_ok;
    }
  }
  std::cout << "packets=" << result.packets.size() << " crc_ok=" << crc_ok
            << '\n';
}

void print_new_packets(const g474::DecodeResult &result,
                       std::set<std::string> &printed,
                       std::ofstream *events_csv = nullptr,
                       std::mutex *events_mutex = nullptr) {
  for (const auto &packet : result.packets) {
    const std::string key = std::to_string(packet.channel) + ":" +
                            std::to_string(packet.record_index) + ":" +
                            std::to_string(packet.bit_offset) + ":" +
                            std::to_string(packet.symbol_index);
    if (printed.insert(key).second) {
      std::lock_guard<std::mutex> lock(g_print_mutex);
      std::cout << g474::format_packet(packet) << '\n';
      if (events_csv && *events_csv) {
        if (events_mutex) {
          std::lock_guard<std::mutex> event_lock(*events_mutex);
          *events_csv << pd_event_csv_row(packet);
        } else {
          *events_csv << pd_event_csv_row(packet);
        }
      }
    }
  }
}

g474::DecodeConfig config_from_args(const Args &args) {
  g474::DecodeConfig config;
  config.include_overflow = args.include_overflow;
  config.include_crc_bad = args.show_crc_bad;
  return config;
}

int decode_file(const Args &args) {
  const auto bytes = read_file(args.in_bin);
  const auto records = g474::records_from_bytes(bytes);
  g474::PdDecoder decoder(config_from_args(args));

  for (std::size_t i = 0; i < records.size(); ++i) {
    decoder.add_record(i, records[i]);
  }

  const auto result = decoder.decode();
  std::ofstream events_csv;
  if (!args.out_events_csv.empty()) {
    events_csv.open(args.out_events_csv);
    if (!events_csv) {
      throw std::runtime_error("Unable to open events CSV output: " +
                               args.out_events_csv);
    }
    events_csv << events_csv_header();
    for (const auto &packet : result.packets) {
      events_csv << pd_event_csv_row(packet);
    }
  }
  std::cout << "sample_clock=24000000Hz sample_width=16 half=30..50 "
               "full=70..100 max_gap=400\n";
  print_summary(result);
  for (const auto &packet : result.packets) {
    std::cout << g474::format_packet(packet) << '\n';
  }
  return 0;
}

int decode_analog_file(const Args &args) {
  const auto bytes = read_file(args.in_ana_bin);
  const auto records = g474::records_from_bytes(bytes);

  std::ofstream csv_out;
  if (!args.out_ana_csv.empty()) {
    csv_out.open(args.out_ana_csv);
    if (!csv_out) {
      throw std::runtime_error("Unable to open analog CSV output: " +
                               args.out_ana_csv);
    }
    csv_out << g474::analog_csv_header();
  }

  std::ofstream events_csv;
  if (!args.out_events_csv.empty()) {
    events_csv.open(args.out_events_csv);
    if (!events_csv) {
      throw std::runtime_error("Unable to open events CSV output: " +
                               args.out_events_csv);
    }
    events_csv << events_csv_header();
  }

  uint64_t analog_count = 0;
  uint64_t unknown_count = 0;
  for (std::size_t i = 0; i < records.size(); ++i) {
    auto snapshot = g474::parse_analog_record(i, records[i]);
    if (!snapshot) {
      ++unknown_count;
      continue;
    }
    ++analog_count;
    if (csv_out) {
      csv_out << g474::analog_csv_row(*snapshot);
    }
    if (events_csv) {
      events_csv << analog_event_csv_row(*snapshot);
    }
    if (analog_count <= 8) {
      std::cout << g474::format_analog(*snapshot) << '\n';
    }
  }

  std::cout << "Analog file summary: records=" << records.size()
            << " analog=" << analog_count << " unknown=" << unknown_count
            << '\n';
  return 0;
}

struct TimelineRow {
  uint64_t timestamp_us = 0;
  std::string row;
};

int decode_combined_files(const Args &args) {
  if (args.out_events_csv.empty()) {
    throw std::runtime_error(
        "--out-csv is required when combining PD and analog input files");
  }

  const auto pd_bytes = read_file(args.in_bin);
  const auto pd_records = g474::records_from_bytes(pd_bytes);
  g474::PdDecoder decoder(config_from_args(args));
  for (std::size_t i = 0; i < pd_records.size(); ++i) {
    decoder.add_record(i, pd_records[i]);
  }
  const auto result = decoder.decode();

  const auto analog_bytes = read_file(args.in_ana_bin);
  const auto analog_records = g474::records_from_bytes(analog_bytes);

  std::vector<TimelineRow> rows;
  rows.reserve(result.packets.size() + analog_records.size());
  for (const auto &packet : result.packets) {
    rows.push_back({packet.timestamp_us, pd_event_csv_row(packet)});
  }

  uint64_t analog_count = 0;
  uint64_t unknown_count = 0;
  for (std::size_t i = 0; i < analog_records.size(); ++i) {
    auto snapshot = g474::parse_analog_record(i, analog_records[i]);
    if (!snapshot) {
      ++unknown_count;
      continue;
    }
    ++analog_count;
    rows.push_back({snapshot->timestamp_us, analog_event_csv_row(*snapshot)});
  }

  std::stable_sort(rows.begin(), rows.end(),
                   [](const TimelineRow &a, const TimelineRow &b) {
                     return a.timestamp_us < b.timestamp_us;
                   });

  std::ofstream events_csv(args.out_events_csv);
  if (!events_csv) {
    throw std::runtime_error("Unable to open CSV output: " +
                             args.out_events_csv);
  }
  events_csv << events_csv_header();
  for (const auto &row : rows) {
    events_csv << row.row;
  }

  print_summary(result);
  std::cout << "Analog file summary: records=" << analog_records.size()
            << " analog=" << analog_count << " unknown=" << unknown_count
            << '\n';
  std::cout << "combined_csv=" << args.out_events_csv
            << " rows=" << rows.size() << '\n';
  return 0;
}

std::ofstream open_optional_output(const std::string &path,
                                   const std::string &label) {
  std::ofstream raw_out;
  if (!path.empty()) {
    raw_out.open(path, std::ios::binary);
    if (!raw_out) {
      throw std::runtime_error("Unable to open " + label + " output: " + path);
    }
  }
  return raw_out;
}

void pd_loop(g474::UsbDevice &dev, const Args &args,
             std::ofstream *events_csv = nullptr,
             std::mutex *events_mutex = nullptr) {
  std::ofstream raw_out = open_optional_output(args.out_raw, "PD raw");
  g474::PdDecoder decoder(config_from_args(args));
  std::vector<uint8_t> partial;
  std::set<std::string> printed;
  uint64_t record_index = 0;

  while (!g_stop && record_index < args.max_records) {
    auto data = dev.bulk_read(args.endpoint, args.read_size, args.timeout_ms);
    if (data.empty()) {
      continue;
    }

    if (raw_out) {
      raw_out.write(reinterpret_cast<const char *>(data.data()),
                    static_cast<std::streamsize>(data.size()));
    }

    partial.insert(partial.end(), data.begin(), data.end());
    while (partial.size() >= g474::kRecordSize && record_index < args.max_records) {
      g474::RawRecord record{};
      std::copy(partial.begin(), partial.begin() + g474::kRecordSize,
                record.begin());
      partial.erase(partial.begin(), partial.begin() + g474::kRecordSize);

      decoder.add_record(record_index, record);
      ++record_index;

      if ((record_index % 32u) == 0u) {
        const auto result = decoder.decode();
        print_new_packets(result, printed, events_csv, events_mutex);
      }
    }
  }

  const auto result = decoder.decode();
  print_new_packets(result, printed, events_csv, events_mutex);
  {
    std::lock_guard<std::mutex> lock(g_print_mutex);
    std::cout << "PD summary:\n";
    print_summary(result);
    if (!partial.empty()) {
      std::cout << "PD trailing partial bytes not analyzed: " << partial.size()
                << '\n';
    }
  }
}

void analog_loop(g474::UsbDevice &dev, const Args &args,
                 std::ofstream *events_csv = nullptr,
                 std::mutex *events_mutex = nullptr) {
  std::ofstream raw_out = open_optional_output(args.out_ana_raw, "analog raw");
  std::ofstream csv_out;
  if (!args.out_ana_csv.empty()) {
    csv_out.open(args.out_ana_csv);
    if (!csv_out) {
      throw std::runtime_error("Unable to open analog CSV output: " +
                               args.out_ana_csv);
    }
    csv_out << g474::analog_csv_header();
  }

  std::vector<uint8_t> partial;
  uint64_t record_index = 0;
  uint64_t analog_count = 0;
  uint64_t unknown_count = 0;

  while (!g_stop && record_index < args.max_records) {
    auto data =
        dev.bulk_read(args.analog_endpoint, args.read_size, args.timeout_ms);
    if (data.empty()) {
      continue;
    }

    if (raw_out) {
      raw_out.write(reinterpret_cast<const char *>(data.data()),
                    static_cast<std::streamsize>(data.size()));
    }

    partial.insert(partial.end(), data.begin(), data.end());
    while (partial.size() >= g474::kRecordSize &&
           record_index < args.max_records) {
      g474::RawRecord record{};
      std::copy(partial.begin(), partial.begin() + g474::kRecordSize,
                record.begin());
      partial.erase(partial.begin(), partial.begin() + g474::kRecordSize);

      auto snapshot = g474::parse_analog_record(record_index, record);
      if (snapshot) {
        ++analog_count;
        if (csv_out) {
          csv_out << g474::analog_csv_row(*snapshot);
        }
        if (events_csv && *events_csv) {
          if (events_mutex) {
            std::lock_guard<std::mutex> event_lock(*events_mutex);
            *events_csv << analog_event_csv_row(*snapshot);
          } else {
            *events_csv << analog_event_csv_row(*snapshot);
          }
        }
        if ((analog_count % 100u) == 1u) {
          std::lock_guard<std::mutex> lock(g_print_mutex);
          std::cout << g474::format_analog(*snapshot) << '\n';
        }
      } else {
        ++unknown_count;
      }

      ++record_index;
    }
  }

  {
    std::lock_guard<std::mutex> lock(g_print_mutex);
    std::cout << "Analog summary: records=" << record_index
              << " analog=" << analog_count << " unknown=" << unknown_count
              << '\n';
    if (!partial.empty()) {
      std::cout << "Analog trailing partial bytes not analyzed: "
                << partial.size() << '\n';
    }
  }
}

int live_capture(const Args &args) {
  if (args.read_size < static_cast<int>(g474::kRecordSize)) {
    throw std::runtime_error("--read-size must be at least 64");
  }

  std::signal(SIGINT, signal_handler);
  g474::UsbDevice dev(args.vid, args.pid, args.interface_number);
  std::ofstream events_csv;
  std::mutex events_mutex;
  if (!args.out_events_csv.empty()) {
    events_csv.open(args.out_events_csv);
    if (!events_csv) {
      throw std::runtime_error("Unable to open events CSV output: " +
                               args.out_events_csv);
    }
    events_csv << events_csv_header();
  }

  {
    std::lock_guard<std::mutex> lock(g_print_mutex);
    std::cout << "Reading VID=0x" << std::hex << args.vid << " PID=0x"
              << args.pid << " PD_EP=0x" << static_cast<unsigned>(args.endpoint)
              << " ANA_EP=0x" << static_cast<unsigned>(args.analog_endpoint)
              << std::dec << " records=" << args.max_records << '\n';
  }

  if (args.mode == StreamMode::Pd) {
    pd_loop(dev, args, &events_csv, &events_mutex);
  } else if (args.mode == StreamMode::Analog) {
    analog_loop(dev, args, &events_csv, &events_mutex);
  } else {
    std::thread pd_thread(pd_loop, std::ref(dev), std::cref(args),
                          &events_csv, &events_mutex);
    std::thread analog_thread(analog_loop, std::ref(dev), std::cref(args),
                              &events_csv, &events_mutex);
    pd_thread.join();
    analog_thread.join();
  }

  return 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Args args = parse_args(argc, argv);
    if (!args.in_bin.empty() && !args.in_ana_bin.empty()) {
      return decode_combined_files(args);
    }
    if (!args.in_bin.empty()) {
      return decode_file(args);
    }
    if (!args.in_ana_bin.empty()) {
      return decode_analog_file(args);
    }
    return live_capture(args);
  } catch (const std::exception &e) {
    std::cerr << "error: " << e.what() << '\n';
    std::cerr << "Use --help for usage.\n";
    return 1;
  }
}
