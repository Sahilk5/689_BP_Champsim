// bpchar: characterize each static conditional branch with idealized reference predictors.
//
// For every static conditional branch, over the measured window, it records executions, taken
// count, outcome transitions, and the mispredictions of online-learned 2-bit-counter predictors:
//   bimodal      one counter per static branch
//   local<h>     one counter per (branch, its own last h outcomes),           h in LOCAL_LENGTHS
//   global<h>    one counter per (branch, last h outcomes of all branches),   h in GLOBAL_LENGTHS
// The tables are keyed by a 62-bit hash of (branch, history) and grow as needed, so they are unaliased
// up to 62-bit hash collisions; the summary reports how many patterns each table holds. Global history includes every branch type, as the
// predictors under study use it. Counters start weakly not-taken (1 of 0..3).
//
// Usage: bpchar --stream F.bstc [--warmup W] [--sim S] --out branches.csv [--summary summary.json]
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "stream_format.h"

namespace
{
constexpr unsigned LOCAL_LENGTHS[] = {4, 8, 16, 32};
constexpr unsigned GLOBAL_LENGTHS[] = {2, 4, 8, 16, 32, 64, 128};
constexpr std::size_t N_LOCAL = std::size(LOCAL_LENGTHS), N_GLOBAL = std::size(GLOBAL_LENGTHS);
constexpr uint8_t CONDITIONAL = 2;

inline uint64_t mix(uint64_t x)
{
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return x;
}

// Open-addressing table of 2-bit counters; slot = (62-bit key << 2) | counter, 0 = empty.
// It doubles when half full, so it never evicts: every (branch, history) pattern keeps its own counter.
class CounterTable
{
  std::vector<uint64_t> slots_;
  uint64_t mask_;

  void grow()
  {
    std::vector<uint64_t> old;
    old.swap(slots_);
    slots_.assign(old.size() * 2, 0);
    mask_ = slots_.size() - 1;
    for (uint64_t s : old)
      if (s != 0) {
        uint64_t i = mix(s >> 2) & mask_;
        while (slots_[i] != 0)
          i = (i + 1) & mask_;
        slots_[i] = s;
      }
  }

public:
  uint64_t evictions = 0, entries = 0;  // evictions stays 0 (kept for the summary format)
  explicit CounterTable(unsigned log2_slots) : slots_(std::size_t{1} << log2_slots, 0), mask_((uint64_t{1} << log2_slots) - 1) {}

  // Returns the slot for key, inserting it with counter 1 (weakly not taken) if absent.
  uint64_t& slot(uint64_t key)
  {
    key = (key | 1) & ((uint64_t{1} << 62) - 1);  // never 0
    const uint64_t tag = key << 2;
    if (2 * (entries + 1) > slots_.size())
      grow();
    uint64_t i = mix(key) & mask_;
    while (true) {
      uint64_t& s = slots_[i];
      if (s == 0) {
        s = tag | 1;
        ++entries;
        return s;
      }
      if ((s & ~uint64_t{3}) == tag)
        return s;
      i = (i + 1) & mask_;
    }
  }
};

inline bool predict_and_update(uint64_t& s, bool taken)
{
  const uint64_t c = s & 3;
  const bool pred = c >= 2;
  if (taken && c < 3)
    ++s;
  if (!taken && c > 0)
    --s;
  return pred;
}

struct BranchStats {
  uint64_t execs = 0, taken = 0, transitions = 0, mis_bimodal = 0;
  std::array<uint64_t, N_LOCAL> mis_local{};
  std::array<uint64_t, N_GLOBAL> mis_global{};
};
}  // namespace

int main(int argc, char** argv)
{
  try {
    std::string stream, out, summary;
    uint64_t warmup = 50'000'000, sim = 100'000'000;
    for (int i = 1; i + 1 < argc; i += 2) {
      std::string k = argv[i], v = argv[i + 1];
      if (k == "--stream")
        stream = v;
      else if (k == "--out")
        out = v;
      else if (k == "--summary")
        summary = v;
      else if (k == "--warmup")
        warmup = std::stoull(v);
      else if (k == "--sim")
        sim = std::stoull(v);
      else
        throw std::runtime_error("unknown argument " + k);
    }
    if (stream.empty() || out.empty())
      throw std::runtime_error("usage: bpchar --stream F.bstc --out branches.csv [--summary S.json] [--warmup W] [--sim S]");

    int fd = ::open(stream.c_str(), O_RDONLY);
    if (fd < 0)
      throw std::runtime_error("cannot open " + stream);
    struct stat st {};
    ::fstat(fd, &st);
    const auto size = static_cast<std::size_t>(st.st_size);
    void* base = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (base == MAP_FAILED)
      throw std::runtime_error("mmap failed");
    const char* p = static_cast<const char*>(base);
    bst::Header h;
    std::memcpy(&h, p, sizeof h);
    if (h.version != bst::VERSION_COMPACT)
      throw std::runtime_error("bpchar needs a compact (v2) stream");
    const auto* statics = reinterpret_cast<const bst::StaticEntry*>(p + h.header_bytes);
    const auto* records = reinterpret_cast<const uint64_t*>(statics + h.n_static);

    CounterTable bimodal(16);
    std::vector<CounterTable> local, global;
    for (std::size_t k = 0; k < N_LOCAL; ++k)
      local.emplace_back(16);
    for (std::size_t k = 0; k < N_GLOBAL; ++k)
      global.emplace_back(16);
    std::vector<uint64_t> local_hist(h.n_static, 0);
    std::vector<int8_t> last_outcome(h.n_static, -1);
    std::vector<BranchStats> stats(h.n_static);
    std::array<uint64_t, 2> ghist{};  // bit 0 = most recent outcome, 128 bits
    const uint64_t end = warmup + sim;
    uint64_t index_plus_one = 0;

    for (uint64_t r = 0; r < h.n_records; ++r) {
      const uint64_t rec = records[r];
      index_plus_one += rec >> bst::DELTA_SHIFT_V2;
      const uint64_t index = index_plus_one - 1;
      if (index >= end)
        break;
      const auto id = rec & bst::ID_MASK;
      const bool taken = rec & bst::TAKEN_BIT;
      if (statics[id].type == CONDITIONAL) {
        const bool roi = index >= warmup;
        auto& b = stats[id];
        if (roi) {
          ++b.execs;
          b.taken += taken;
          b.transitions += last_outcome[id] >= 0 && static_cast<bool>(last_outcome[id]) != taken;
        }
        last_outcome[id] = taken;
        const uint64_t idh = mix(id + 1);
        b.mis_bimodal += roi && predict_and_update(bimodal.slot(idh), taken) != taken;
        for (std::size_t k = 0; k < N_LOCAL; ++k) {
          const unsigned len = LOCAL_LENGTHS[k];
          const uint64_t hist = len >= 64 ? local_hist[id] : (local_hist[id] & ((uint64_t{1} << len) - 1));
          b.mis_local[k] += roi && predict_and_update(local[k].slot(mix(idh ^ (hist * 0x9E3779B97F4A7C15ULL) ^ len)), taken) != taken;
        }
        for (std::size_t k = 0; k < N_GLOBAL; ++k) {
          const unsigned len = GLOBAL_LENGTHS[k];
          uint64_t lo = len >= 64 ? ghist[0] : (ghist[0] & ((uint64_t{1} << len) - 1));
          uint64_t hi = len > 64 ? (len >= 128 ? ghist[1] : (ghist[1] & ((uint64_t{1} << (len - 64)) - 1))) : 0;
          const uint64_t key = mix(idh ^ mix(lo ^ 0x51) ^ mix(hi ^ 0xA7) ^ len);
          b.mis_global[k] += roi && predict_and_update(global[k].slot(key), taken) != taken;
        }
        local_hist[id] = (local_hist[id] << 1) | (taken ? 1 : 0);
      }
      ghist[1] = (ghist[1] << 1) | (ghist[0] >> 63);
      ghist[0] = (ghist[0] << 1) | (taken ? 1 : 0);
    }

    std::ofstream o(out);
    o << "ip,execs,taken,transitions,mis_bimodal";
    for (unsigned len : LOCAL_LENGTHS)
      o << ",mis_local" << len;
    for (unsigned len : GLOBAL_LENGTHS)
      o << ",mis_global" << len;
    o << "\n";
    for (uint64_t id = 0; id < h.n_static; ++id) {
      const auto& b = stats[id];
      if (b.execs == 0)
        continue;
      o << "0x" << std::hex << statics[id].ip << std::dec << "," << b.execs << "," << b.taken << "," << b.transitions << "," << b.mis_bimodal;
      for (auto m : b.mis_local)
        o << "," << m;
      for (auto m : b.mis_global)
        o << "," << m;
      o << "\n";
    }
    if (!summary.empty()) {
      std::ofstream s(summary);
      s << "{\"stream\": \"" << stream << "\", \"warmup\": " << warmup << ", \"sim\": " << sim << ", \"evictions\": {\"bimodal\": " << bimodal.evictions;
      for (std::size_t k = 0; k < N_LOCAL; ++k)
        s << ", \"local" << LOCAL_LENGTHS[k] << "\": " << local[k].evictions;
      for (std::size_t k = 0; k < N_GLOBAL; ++k)
        s << ", \"global" << GLOBAL_LENGTHS[k] << "\": " << global[k].evictions;
      s << "}, \"entries\": {\"bimodal\": " << bimodal.entries;
      for (std::size_t k = 0; k < N_LOCAL; ++k)
        s << ", \"local" << LOCAL_LENGTHS[k] << "\": " << local[k].entries;
      for (std::size_t k = 0; k < N_GLOBAL; ++k)
        s << ", \"global" << GLOBAL_LENGTHS[k] << "\": " << global[k].entries;
      s << "}}\n";
    }
    ::munmap(base, size);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "bpchar: " << e.what() << "\n";
    return 1;
  }
}
