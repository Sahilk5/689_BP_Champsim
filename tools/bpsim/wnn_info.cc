// wnn_info: load a wnn config and report its storage and input coverage, without simulating.
// Usage: wnn_info --config C.json [--rams N --tuple-bits n --counter-bits q --seed s]
// Coverage groups local-history sources by bit index ("local"), because all local tables are
// truncations of the same history when they share size and index (as in the original design).
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

#include "branch/wnn/wnn_core.h"

int main(int argc, char** argv)
{
  try {
    std::string path;
    long rams = -1, tuple_bits = -1, counter_bits = -1, seed = -1;
    for (int i = 1; i < argc; ++i) {
      std::string k = argv[i];
      if (i + 1 >= argc)
        throw std::runtime_error("missing value for " + k);
      std::string v = argv[++i];
      if (k == "--config")
        path = v;
      else if (k == "--rams")
        rams = std::stol(v);
      else if (k == "--tuple-bits")
        tuple_bits = std::stol(v);
      else if (k == "--counter-bits")
        counter_bits = std::stol(v);
      else if (k == "--seed")
        seed = std::stol(v);
      else
        throw std::runtime_error("unknown argument " + k);
    }
    auto cfg = wnn_core::Config::from_json_file(path);
    if (rams > 0)
      cfg.rams = static_cast<unsigned>(rams);
    if (tuple_bits > 0)
      cfg.tuple_bits = static_cast<unsigned>(tuple_bits);
    if (counter_bits > 0)
      cfg.counter_bits = static_cast<unsigned>(counter_bits);
    if (seed >= 0)
      cfg.seed = static_cast<uint32_t>(seed);
    wnn_core::Predictor p(cfg);
    const auto s = p.storage();

    auto family = [&](uint16_t src) {
      const auto& sp = p.sources()[src];
      return sp.source == "lhr" ? std::string("local") : sp.source;
    };
    std::map<std::string, std::set<unsigned>> covered;
    std::map<std::string, unsigned> width;
    for (std::size_t k = 0; k < p.sources().size(); ++k) {
      auto f = family(static_cast<uint16_t>(k));
      width[f] = std::max(width[f], p.sources()[k].width);
    }
    unsigned dup_rams = 0, wasted = 0;
    for (const auto& tuple : p.mapping()) {
      std::set<std::pair<std::string, unsigned>> distinct;
      for (int pos : tuple) {
        auto [src, bit] = p.pool_position(pos);
        distinct.insert({family(src), bit});
        covered[family(src)].insert(bit);
      }
      if (distinct.size() < tuple.size()) {
        ++dup_rams;
        wasted += static_cast<unsigned>(tuple.size() - distinct.size());
      }
    }
    std::cout << "{\"name\": \"" << cfg.name << "\", \"rams\": " << cfg.rams << ", \"tuple_bits\": " << cfg.tuple_bits << ", \"counter_bits\": " << cfg.counter_bits
              << ", \"seed\": " << cfg.seed << ", \"pool_positions\": " << p.pool_size() << ",\n \"storage_bits\": {\"discriminators\": " << s.discriminator_bits
              << ", \"local_history\": " << s.lht_bits << ", \"global_history\": " << s.global_history_bits << ", \"path_history\": " << s.path_history_bits
              << ", \"mpp_state\": " << s.mpp_state_bits << ", \"total\": " << s.total_bits() << "},\n \"storage_kib\": " << static_cast<double>(s.total_bits()) / 8192.0
              << ",\n \"coverage\": {";
    bool first = true;
    for (const auto& [f, bits] : covered) {
      std::cout << (first ? "" : ", ") << "\"" << f << "\": [" << bits.size() << ", " << width[f] << "]";
      first = false;
    }
    std::cout << "},\n \"rams_with_repeated_bit\": " << dup_rams << ", \"wasted_address_bits\": " << wasted << "}\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "wnn_info: " << e.what() << "\n";
    return 1;
  }
}
