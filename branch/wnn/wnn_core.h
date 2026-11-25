// wnn_core: a configurable WiSARD (weightless neural network) branch predictor.
//
// One implementation exposes every knob of the sensitivity study (final-tianfangguo/docs/sweep_plan.md)
// as a run-time setting (a JSON config). Every knob defaults to the original design. Two presets
// reproduce existing predictors bit for bit:
//   presets/original.json : branch/wisard (main, 3ea074e), including its speculative-history bug
//   presets/mpp_v2.json   : Sahil Kaushal's wisard_mpp_v2 (origin/sahil/abalation_v1)
//
// Equivalence notes (why a faster implementation gives identical predictions):
//  * A RAM node's address is a fixed function of the input bits. Any fixed one-to-one relabeling
//    of its addresses (reordering the tuple bits, or dropping a bit that duplicates another bit of
//    the same tuple) leaves every prediction unchanged, because counters start at zero and are only
//    read and written through that same function. Addresses are therefore built with one bit-gather
//    (pext) per (RAM, source word) instead of materializing the 2158-bit input vector.
//  * Bleaching (raise the threshold from 1 until the two responses differ) is evaluated only at
//    thresholds where a response can change: the start threshold and each stored counter value.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wnn_core
{

enum class HistoryPolicy { SpecEveryCall, UpdateOnly };
enum class TypeFilter { All, Conditional, Taken };
enum class TieRule { BleachLinear, BleachStep2, BleachDoubling, SumCounters, MaxCounter };
enum class DefaultRule { NotTaken, Taken, BiasTable, LastOutcome };
enum class TrainSide { One, Two };
enum class TrainGate { Always, Mispredict, Margin, Adaptive };
enum class DecayMethod { Halve, Subtract1, None };
enum class Saturation { Saturate, HalvePair };
enum class Mode { Discriminators, Signed };
enum class Fallback { None, Chooser };

struct SourceSpec {
  std::string source;    // pc, ghr, pc_xor_ghr, lhr, gphr, modhist5, modhist7, ghistpath, blurry, recencypos, imli_nz, imli, btype, folded_ghr,
                         // pc_fold_ghr, pc_fold_lhr, pc_fold_path: folded pc xor a folded history window (hashing, not raw bits)
  unsigned width = 0;    // bits taken from the source (<= 64)
  unsigned shift = 0;    // pc / pc_xor_ghr: right shift of ip; ghistpath: left shift of ghr; blurry: right shift of ghr
  unsigned offset = 0;   // ghr / gphr / folded_ghr: first history bit
  unsigned table = 0;    // lhr: which local history table
  unsigned ghr_bits = 0; // ghistpath / blurry: truncate ghr to this many bits first (0 = whole ghr)
  unsigned pc_bits = 24; // ghistpath: pc bits xored in
  unsigned length = 0;   // folded_ghr, pc_fold_*: history bits folded (xor) into `width` bits
  unsigned salt = 0;     // pc_fold_*: rotate the folded history by this many bits, so RAMs differ
  unsigned copies = 1;   // replication in the input pool (flat mapping)
  double weight = 1.0;   // sampling weight (weighted and coverage mappings)
  bool operator==(const SourceSpec& o) const;  // equal as a computed word (ignores copies and weight)
};

struct Config {
  std::string name = "unnamed";
  unsigned rams = 64, tuple_bits = 10, counter_bits = 8;
  std::vector<unsigned> tuple_bits_per_ram;  // optional: per-RAM tuple widths (mixed widths)
  HistoryPolicy history = HistoryPolicy::SpecEveryCall;
  TypeFilter history_types = TypeFilter::All;  // which branches update global, local and MPP history
  TypeFilter path_types = TypeFilter::All;     // which branches update path history
  unsigned ghr_length = 24;
  struct Lht {
    unsigned entries = 1024, length = 24;
  };
  std::vector<Lht> lhts;
  unsigned gphr_bits_per_branch = 8, gphr_length = 64;
  unsigned recency_depth = 56, recency_buckets = 64;
  unsigned mod5_depth = 8, mod7_depth = 9;
  unsigned imli_bits = 6;
  std::vector<SourceSpec> pool;
  std::string mapping = "flat_shuffle";  // flat_shuffle | weighted | coverage | structured | explicit
  std::vector<std::vector<int>> tuples;  // explicit mapping: pool positions of each RAM's tuple
  uint32_t seed = 67;
  unsigned structured_pc_bits = 4, structured_window_min = 4, structured_window_max = 64;
  unsigned base_threshold = 0;
  TieRule tie = TieRule::BleachLinear;
  DefaultRule default_rule = DefaultRule::NotTaken;
  unsigned default_table_entries = 4096;  // bias_table / last_outcome
  Mode mode = Mode::Discriminators;
  TrainSide train_side = TrainSide::One;
  TrainGate train_gate = TrainGate::Always;
  unsigned theta = 0;
  TypeFilter train_types = TypeFilter::All;
  uint64_t decay_interval = 1ULL << 18;  // 0 = never
  DecayMethod decay = DecayMethod::Halve;
  Saturation saturation = Saturation::Saturate;
  Fallback fallback = Fallback::None;
  unsigned fallback_entries = 4096;  // bimodal and chooser tables

  static Config from_json_text(const std::string& text);
  static Config from_json_file(const std::string& path);
};

struct Storage {
  uint64_t discriminator_bits = 0, lht_bits = 0, global_history_bits = 0, path_history_bits = 0, mpp_state_bits = 0, side_table_bits = 0;
  uint64_t total_bits() const
  {
    return discriminator_bits + lht_bits + global_history_bits + path_history_bits + mpp_state_bits + side_table_bits;
  }
};

struct Stats {
  uint64_t predictions = 0, ties = 0, unresolved_ties = 0, bleach_steps_sum = 0, bleach_steps_max = 0, trainings = 0, chooser_used_bimodal = 0;
};

class Predictor
{
public:
  explicit Predictor(Config cfg);
  bool predict(uint64_t ip, uint8_t branch_type = 7);
  void update(uint64_t ip, uint64_t target, bool taken, uint8_t branch_type);
  Storage storage() const;
  const Stats& stats() const { return stats_; }
  const Config& config() const { return cfg_; }
  unsigned pool_size() const { return static_cast<unsigned>(pool_positions_.size()); }
  // RAM i's tuple as pool positions, in mapping order (for coverage checks)
  const std::vector<std::vector<int>>& mapping() const { return mapping_; }
  const std::vector<SourceSpec>& sources() const { return sources_; }
  std::pair<uint16_t, uint8_t> pool_position(int pos) const { return pool_positions_[static_cast<std::size_t>(pos)]; }

private:
  enum class Kind : uint8_t { Pc, Ghr, PcXorGhr, Lhr, Gphr, Modhist5, Modhist7, Ghistpath, Blurry, Recencypos, ImliNz, Imli, Btype, FoldedGhr, PcFoldGhr, PcFoldLhr, PcFoldPath };
  struct Group {
    uint16_t source;
    uint8_t shift;
    uint64_t mask;
  };
  struct HistoryState {
    std::vector<uint64_t> ghr;  // bit 0 = most recent outcome
    std::vector<uint32_t> recency;
    std::vector<uint8_t> mod5, mod7;
  };

  void build_sources();
  void build_mapping();
  void build_groups();
  void compute_words(uint64_t ip, uint8_t branch_type, const HistoryState& h);
  bool decide_discriminators(const uint32_t* addr);
  bool decide_signed(const uint32_t* addr);
  bool resolve_tie();
  bool default_prediction();
  void train(bool taken);
  void push_ghr(std::vector<uint64_t>& ghr, bool bit) const;
  uint64_t ghr_bits(const std::vector<uint64_t>& ghr, unsigned offset, unsigned width) const;
  uint32_t recency_position(const HistoryState& h, uint32_t pc_shifted) const;
  static void insert_recency(std::vector<uint32_t>& stack, uint32_t pc_shifted);
  static void push_history(std::vector<uint8_t>& hist, uint8_t value);
  void decay_all();
  static std::size_t side_index(uint64_t ip, std::size_t entries);
  static bool passes(TypeFilter f, bool conditional, bool taken);

  Config cfg_;
  Stats stats_;
  uint32_t max_count_ = 0;
  int32_t signed_max_ = 0, signed_min_ = 0;
  std::vector<SourceSpec> sources_;  // distinct source words
  std::vector<Kind> kinds_;
  std::vector<std::pair<uint16_t, uint8_t>> pool_positions_;  // (source, bit) per pool position
  std::vector<unsigned> widths_;   // tuple bits per RAM
  std::vector<std::size_t> base_;  // first entry of RAM i
  std::vector<std::vector<int>> mapping_;
  std::vector<std::vector<Group>> ram_groups_;
  std::vector<uint64_t> words_;
  std::vector<uint16_t> counters_;  // discriminators: [entry][class], class 0 = not taken, 1 = taken
  std::vector<int16_t> signed_;     // signed mode: one counter per entry
  std::vector<uint32_t> addr_, last_addr_;
  std::vector<uint16_t> c_t_, c_nt_;
  std::vector<uint32_t> cand_;
  std::vector<uint16_t> st_, snt_;
  bool last_pred_ = false, last_wnn_pred_ = false, last_bimodal_pred_ = false;
  uint32_t last_margin_ = 0;
  uint64_t last_ip_ = 0;
  HistoryState spec_, real_;
  std::vector<std::vector<uint64_t>> lht_;
  uint64_t gphr_ = 0;
  uint32_t imli_ = 0;
  uint64_t since_decay_ = 0;
  bool uses_mpp_ = false;
  std::vector<uint8_t> default_table_;  // bias_table: 2-bit counters; last_outcome: 1 bit
  std::vector<uint8_t> bimodal_, chooser_;
  unsigned theta_ = 0;  // adaptive training threshold
  int tc_ = 0;
};

}  // namespace wnn_core
