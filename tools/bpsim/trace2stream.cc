// trace2stream: decode a ChampSim trace once into a branch-stream cache (.bst).
//
// Decoding uses ChampSim's own reader (get_tracereader with repeat=true, exactly as
// src/main.cc does when --simulation-instructions is given), so branch types, branch
// targets and wrap-around at end of trace are identical to a ChampSim run.
//
// Usage:
//   trace2stream --trace T --out F.bst [--instructions N]   decode the first N instructions
//   trace2stream --trace T --count                           count the whole trace, no output file
// Both print a JSON summary on stdout.
#include <array>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "instruction.h"
#include "stream_format.h"
#include "tracereader.h"

namespace
{
struct Args {
  std::string trace, out;
  uint64_t instructions = 151'000'000;  // 50M warmup + 100M simulation + 1M margin for fetch run-ahead
  bool count = false;
};

Args parse(int argc, char** argv)
{
  Args a;
  for (int i = 1; i < argc; ++i) {
    std::string k = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc)
        throw std::runtime_error("missing value for " + k);
      return argv[++i];
    };
    if (k == "--trace")
      a.trace = next();
    else if (k == "--out")
      a.out = next();
    else if (k == "--instructions")
      a.instructions = std::stoull(next());
    else if (k == "--count")
      a.count = true;
    else
      throw std::runtime_error("unknown argument " + k);
  }
  if (a.trace.empty() || (!a.count && a.out.empty()))
    throw std::runtime_error("usage: trace2stream --trace T (--out F.bst [--instructions N] | --count)");
  return a;
}

void print_counts(const char* mode, const Args& a, uint64_t n_instr, const std::array<uint64_t, 8>& by_type, const std::array<uint64_t, 8>& taken_by_type,
                  uint64_t extra_a, uint64_t extra_b, uint64_t extra_c)
{
  std::printf("{\"mode\": \"%s\", \"trace\": \"%s\", \"instructions\": %llu, \"branches\": {", mode, a.trace.c_str(), static_cast<unsigned long long>(n_instr));
  for (std::size_t t = 0; t + 1 < by_type.size(); ++t)
    std::printf("%s\"%s\": [%llu, %llu]", t ? ", " : "", bst::TYPE_NAMES[t], static_cast<unsigned long long>(by_type[t]),
                static_cast<unsigned long long>(taken_by_type[t]));
  if (std::strcmp(mode, "count") == 0)
    std::printf("}}\n");
  else
    std::printf("}, \"n_static\": %llu, \"n_records\": %llu, \"n_touch\": %llu}\n", static_cast<unsigned long long>(extra_a), static_cast<unsigned long long>(extra_b),
                static_cast<unsigned long long>(extra_c));
}

int count_whole_trace(const Args& a)
{
  auto reader = get_tracereader(a.trace, 0, false, false);
  std::array<uint64_t, 8> by_type{}, taken_by_type{};
  uint64_t n = 0;
  while (!reader.eof()) {
    auto instr = reader();
    ++n;
    if (instr.is_branch) {
      ++by_type[instr.branch];
      taken_by_type[instr.branch] += instr.branch_taken;
    }
  }
  print_counts("count", a, n, by_type, taken_by_type, 0, 0, 0);
  return 0;
}

int decode(const Args& a)
{
  auto reader = get_tracereader(a.trace, 0, false, true);
  std::vector<bst::StaticEntry> statics;
  std::unordered_map<uint64_t, uint32_t> static_id;  // key: ip * 8 + type
  std::unordered_map<uint64_t, uint32_t> group_id;   // key: ip >> 2 -> static id of a branch in that group
  std::vector<uint64_t> records, overrides;
  records.reserve(a.instructions / 4);
  std::array<uint64_t, 8> by_type{}, taken_by_type{};
  uint64_t last = 0, n_touch = 0;

  for (uint64_t n = 0; n < a.instructions; ++n) {
    auto instr = reader();
    uint64_t ip = instr.ip.to<uint64_t>();
    uint64_t delta = n + 1 - last;
    if (delta > 0xFFFFFFFFULL)
      throw std::runtime_error("gap between records exceeds 32 bits");
    if (instr.is_branch) {
      uint8_t type = static_cast<uint8_t>(instr.branch);
      uint64_t key = ip * 8 + type;
      auto [it, inserted] = static_id.try_emplace(key, static_cast<uint32_t>(statics.size()));
      if (inserted) {
        if (statics.size() > bst::ID_MASK)
          throw std::runtime_error("too many static branches");
        statics.push_back(bst::StaticEntry{ip, 0, type, {}});
      }
      auto& st = statics[it->second];
      uint64_t target = instr.branch_target.to<uint64_t>();
      bool override_target = false;
      if (instr.branch_taken) {
        if (st.taken_target == 0)
          st.taken_target = target;
        else if (st.taken_target != target)
          override_target = true;
      }
      if (override_target)
        overrides.push_back(target);
      records.push_back(bst::make_record(it->second, false, instr.branch_taken, override_target, static_cast<uint32_t>(delta)));
      group_id.try_emplace(ip >> 2, it->second);
      ++by_type[type];
      taken_by_type[type] += instr.branch_taken;
      last = n + 1;
    } else if (auto g = group_id.find(ip >> 2); g != group_id.end()) {
      records.push_back(bst::make_record(g->second, true, false, false, static_cast<uint32_t>(delta)));
      ++n_touch;
      last = n + 1;
    }
  }

  bst::Header h{};
  std::memcpy(h.magic, bst::MAGIC, sizeof h.magic);
  h.version = bst::VERSION;
  h.header_bytes = sizeof(bst::Header);
  h.n_static = statics.size();
  h.n_records = records.size();
  h.n_override = overrides.size();
  h.n_instr_decoded = a.instructions;
  h.trace_wraps = 0;  // the repeatable reader does not expose this; trace lengths come from --count
  std::strncpy(h.trace_path, a.trace.c_str(), sizeof h.trace_path - 1);

  std::string tmp = a.out + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f)
    throw std::runtime_error("cannot open " + tmp);
  bool ok = std::fwrite(&h, sizeof h, 1, f) == 1 && std::fwrite(statics.data(), sizeof(bst::StaticEntry), statics.size(), f) == statics.size()
            && std::fwrite(records.data(), sizeof(uint64_t), records.size(), f) == records.size()
            && std::fwrite(overrides.data(), sizeof(uint64_t), overrides.size(), f) == overrides.size();
  ok = (std::fclose(f) == 0) && ok;
  if (!ok || std::rename(tmp.c_str(), a.out.c_str()) != 0)
    throw std::runtime_error("failed writing " + a.out);

  print_counts("decode", a, a.instructions, by_type, taken_by_type, statics.size(), records.size(), n_touch);
  return 0;
}
}  // namespace

int main(int argc, char** argv)
{
  try {
    Args a = parse(argc, argv);
    return a.count ? count_whole_trace(a) : decode(a);
  } catch (const std::exception& e) {
    std::cerr << "trace2stream: " << e.what() << "\n";
    return 1;
  }
}
