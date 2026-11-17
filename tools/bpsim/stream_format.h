// Branch-stream cache format (".bst"): a compact replay of everything ChampSim's
// front end does with the branch predictor and BTB, decoded once per trace.
//
// ChampSim (src/ooo_cpu.cc, do_predict_branch) calls, for EVERY instruction in trace order:
//   btb_prediction(ip); predict_branch(ip, ...);
// and, for branches only, immediately afterwards:
//   update_btb(...); last_branch_result(...).
// A predictor whose predict_branch has no side effects only needs the branch records.
// The BTB lookup does have a side effect (an LRU touch on a hit). BTB entries are keyed by
// ip >> 2, so a non-branch instruction in the same 4-byte group as a branch can hit. Those
// lookups are recorded as TOUCH records so that a replay reproduces the BTB state exactly.
//
// Layout: Header | StaticEntry[n_static] | uint64 record[n_records] | uint64 override_target[n_override]
#pragma once
#include <cstdint>

namespace bst {

// ChampSim's branch_type_names (inc/instruction.h) has no entry for BRANCH_OTHER (6), so keep our own.
constexpr const char* TYPE_NAMES[7] = {"BRANCH_DIRECT_JUMP", "BRANCH_INDIRECT", "BRANCH_CONDITIONAL", "BRANCH_DIRECT_CALL",
                                       "BRANCH_INDIRECT_CALL", "BRANCH_RETURN", "BRANCH_OTHER"};

constexpr char MAGIC[8] = {'B', 'S', 'T', 'R', 'E', 'A', 'M', '1'};
constexpr uint32_t VERSION = 1;

struct Header {
  char magic[8];
  uint32_t version;
  uint32_t header_bytes;
  uint64_t n_static;
  uint64_t n_records;
  uint64_t n_override;
  uint64_t n_instr_decoded;  // instructions decoded, counting wrap-around repeats
  uint64_t trace_wraps;      // times the reader wrapped to the start of the trace
  char trace_path[456];
};
static_assert(sizeof(Header) == 512);

struct StaticEntry {
  uint64_t ip;
  uint64_t taken_target;  // first observed taken target (0 if never taken)
  uint8_t type;           // ChampSim branch_type
  uint8_t pad[7];
};
static_assert(sizeof(StaticEntry) == 24);

// record bit layout
constexpr uint64_t ID_MASK = (1ULL << 28) - 1;  // bits 0-27: static id
constexpr uint64_t TOUCH_BIT = 1ULL << 28;      // non-branch BTB lookup in a branch's group
constexpr uint64_t TAKEN_BIT = 1ULL << 29;
constexpr uint64_t OVERRIDE_BIT = 1ULL << 30;   // taken target differs from StaticEntry::taken_target
constexpr int DELTA_SHIFT = 32;                 // bits 32-63: instructions since the previous record

inline uint64_t make_record(uint64_t id, bool touch, bool taken, bool override_target, uint32_t delta)
{
  return (id & ID_MASK) | (touch ? TOUCH_BIT : 0) | (taken ? TAKEN_BIT : 0) | (override_target ? OVERRIDE_BIT : 0)
         | (uint64_t{delta} << DELTA_SHIFT);
}

}  // namespace bst
