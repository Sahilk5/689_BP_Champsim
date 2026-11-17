#include "wnn_core.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#if defined(__BMI2__)
#include <immintrin.h>
#endif

namespace wnn_core
{
namespace
{
constexpr uint8_t BRANCH_CONDITIONAL_TYPE = 2;  // champsim branch_type BRANCH_CONDITIONAL (checked in wnn.cc)

inline uint64_t mask_bits(unsigned w) { return w >= 64 ? ~uint64_t{0} : ((uint64_t{1} << w) - 1); }

inline uint64_t pext64(uint64_t x, uint64_t m)
{
#if defined(__BMI2__)
  return _pext_u64(x, m);
#else
  uint64_t r = 0;
  for (uint64_t bit = 1; m != 0; bit <<= 1) {
    if (x & m & (~m + 1))
      r |= bit;
    m &= m - 1;
  }
  return r;
#endif
}

using json = nlohmann::json;

void check_keys(const json& j, std::initializer_list<const char*> allowed, const std::string& where)
{
  if (!j.is_object())
    throw std::runtime_error(where + ": expected an object");
  for (auto it = j.begin(); it != j.end(); ++it) {
    if (std::none_of(allowed.begin(), allowed.end(), [&](const char* a) { return it.key() == a; }))
      throw std::runtime_error(where + ": unknown key \"" + it.key() + "\"");
  }
}

template <typename T>
T get_or(const json& j, const char* key, T dflt)
{
  return j.contains(key) ? j.at(key).get<T>() : dflt;
}

template <typename E>
E pick(const std::string& s, std::initializer_list<std::pair<const char*, E>> options, const char* what)
{
  for (const auto& [name, value] : options)
    if (s == name)
      return value;
  throw std::runtime_error(std::string("unknown ") + what + " \"" + s + "\"");
}

TypeFilter parse_filter(const std::string& s)
{
  return pick<TypeFilter>(s, {{"all", TypeFilter::All}, {"conditional", TypeFilter::Conditional}, {"taken", TypeFilter::Taken}}, "branch-type filter");
}

const std::set<std::string> MPP_SOURCES = {"modhist5", "modhist7", "recencypos", "imli_nz", "imli"};
}  // namespace

bool SourceSpec::operator==(const SourceSpec& o) const
{
  return source == o.source && width == o.width && shift == o.shift && offset == o.offset && table == o.table && ghr_bits == o.ghr_bits && pc_bits == o.pc_bits
         && length == o.length;
}

Config Config::from_json_text(const std::string& text)
{
  json j = json::parse(text);
  check_keys(j, {"name", "description", "geometry", "history", "lhts", "path_history", "mpp_state", "pool", "mapping", "predict", "train", "mode", "fallback"},
             "config");
  Config c;
  c.name = get_or<std::string>(j, "name", c.name);
  if (j.contains("geometry")) {
    const auto& g = j.at("geometry");
    check_keys(g, {"rams", "tuple_bits", "counter_bits", "tuple_bits_per_ram"}, "geometry");
    c.rams = get_or(g, "rams", c.rams);
    c.tuple_bits = get_or(g, "tuple_bits", c.tuple_bits);
    c.counter_bits = get_or(g, "counter_bits", c.counter_bits);
    c.tuple_bits_per_ram = get_or(g, "tuple_bits_per_ram", c.tuple_bits_per_ram);
  }
  if (j.contains("history")) {
    const auto& h = j.at("history");
    check_keys(h, {"policy", "types", "path_types", "ghr_length"}, "history");
    c.history = pick<HistoryPolicy>(get_or<std::string>(h, "policy", "spec_every_call"),
                                    {{"spec_every_call", HistoryPolicy::SpecEveryCall}, {"update_only", HistoryPolicy::UpdateOnly}}, "history policy");
    c.history_types = parse_filter(get_or<std::string>(h, "types", "all"));
    c.path_types = parse_filter(get_or<std::string>(h, "path_types", "all"));
    c.ghr_length = get_or(h, "ghr_length", c.ghr_length);
  }
  if (j.contains("lhts")) {
    for (const auto& l : j.at("lhts")) {
      check_keys(l, {"entries", "length"}, "lhts[]");
      c.lhts.push_back(Lht{l.at("entries").get<unsigned>(), l.at("length").get<unsigned>()});
    }
  }
  if (j.contains("path_history")) {
    const auto& p = j.at("path_history");
    check_keys(p, {"bits_per_branch", "length"}, "path_history");
    c.gphr_bits_per_branch = get_or(p, "bits_per_branch", c.gphr_bits_per_branch);
    c.gphr_length = get_or(p, "length", c.gphr_length);
  }
  if (j.contains("mpp_state")) {
    const auto& m = j.at("mpp_state");
    check_keys(m, {"recency_depth", "recency_buckets", "mod5_depth", "mod7_depth", "imli_bits"}, "mpp_state");
    c.recency_depth = get_or(m, "recency_depth", c.recency_depth);
    c.recency_buckets = get_or(m, "recency_buckets", c.recency_buckets);
    c.mod5_depth = get_or(m, "mod5_depth", c.mod5_depth);
    c.mod7_depth = get_or(m, "mod7_depth", c.mod7_depth);
    c.imli_bits = get_or(m, "imli_bits", c.imli_bits);
  }
  for (const auto& s : j.at("pool")) {
    check_keys(s, {"source", "width", "shift", "offset", "table", "ghr_bits", "pc_bits", "length", "copies", "weight"}, "pool[]");
    SourceSpec sp;
    sp.source = s.at("source").get<std::string>();
    sp.width = s.at("width").get<unsigned>();
    sp.shift = get_or(s, "shift", 0u);
    sp.offset = get_or(s, "offset", 0u);
    sp.table = get_or(s, "table", 0u);
    sp.ghr_bits = get_or(s, "ghr_bits", 0u);
    sp.pc_bits = get_or(s, "pc_bits", 24u);
    sp.length = get_or(s, "length", 0u);
    sp.copies = get_or(s, "copies", 1u);
    sp.weight = get_or(s, "weight", 1.0);
    c.pool.push_back(sp);
  }
  if (j.contains("mapping")) {
    const auto& m = j.at("mapping");
    check_keys(m, {"strategy", "seed", "pc_bits", "window_min", "window_max"}, "mapping");
    c.mapping = get_or<std::string>(m, "strategy", c.mapping);
    c.seed = get_or(m, "seed", c.seed);
    c.structured_pc_bits = get_or(m, "pc_bits", c.structured_pc_bits);
    c.structured_window_min = get_or(m, "window_min", c.structured_window_min);
    c.structured_window_max = get_or(m, "window_max", c.structured_window_max);
  }
  if (j.contains("predict")) {
    const auto& p = j.at("predict");
    check_keys(p, {"threshold", "tie", "default", "default_entries"}, "predict");
    c.base_threshold = get_or(p, "threshold", c.base_threshold);
    c.tie = pick<TieRule>(get_or<std::string>(p, "tie", "bleach_linear"),
                          {{"bleach_linear", TieRule::BleachLinear}, {"bleach_step2", TieRule::BleachStep2}, {"bleach_doubling", TieRule::BleachDoubling},
                           {"sum_counters", TieRule::SumCounters}, {"max_counter", TieRule::MaxCounter}},
                          "tie rule");
    c.default_rule = pick<DefaultRule>(get_or<std::string>(p, "default", "not_taken"),
                                       {{"not_taken", DefaultRule::NotTaken}, {"taken", DefaultRule::Taken}, {"bias_table", DefaultRule::BiasTable},
                                        {"last_outcome", DefaultRule::LastOutcome}},
                                       "default rule");
    c.default_table_entries = get_or(p, "default_entries", c.default_table_entries);
  }
  if (j.contains("mode"))
    c.mode = pick<Mode>(j.at("mode").get<std::string>(), {{"discriminators", Mode::Discriminators}, {"signed", Mode::Signed}}, "mode");
  if (j.contains("train")) {
    const auto& t = j.at("train");
    check_keys(t, {"side", "gate", "theta", "types", "decay_interval", "decay", "saturation"}, "train");
    c.train_side = pick<TrainSide>(get_or<std::string>(t, "side", "one"), {{"one", TrainSide::One}, {"two", TrainSide::Two}}, "train side");
    c.train_gate = pick<TrainGate>(get_or<std::string>(t, "gate", "always"),
                                   {{"always", TrainGate::Always}, {"mispredict", TrainGate::Mispredict}, {"margin", TrainGate::Margin}, {"adaptive", TrainGate::Adaptive}},
                                   "train gate");
    c.theta = get_or(t, "theta", c.theta);
    c.train_types = parse_filter(get_or<std::string>(t, "types", "all"));
    c.decay_interval = get_or(t, "decay_interval", c.decay_interval);
    c.decay = pick<DecayMethod>(get_or<std::string>(t, "decay", "halve"), {{"halve", DecayMethod::Halve}, {"subtract1", DecayMethod::Subtract1}, {"none", DecayMethod::None}},
                                "decay");
    c.saturation = pick<Saturation>(get_or<std::string>(t, "saturation", "saturate"), {{"saturate", Saturation::Saturate}, {"halve_pair", Saturation::HalvePair}},
                                    "saturation rule");
  }
  if (j.contains("fallback")) {
    const auto& f = j.at("fallback");
    check_keys(f, {"kind", "entries"}, "fallback");
    c.fallback = pick<Fallback>(get_or<std::string>(f, "kind", "none"), {{"none", Fallback::None}, {"chooser", Fallback::Chooser}}, "fallback");
    c.fallback_entries = get_or(f, "entries", c.fallback_entries);
  }
  return c;
}

Config Config::from_json_file(const std::string& path)
{
  std::ifstream f(path);
  if (!f)
    throw std::runtime_error("cannot open config " + path);
  std::stringstream ss;
  ss << f.rdbuf();
  return from_json_text(ss.str());
}

Predictor::Predictor(Config cfg) : cfg_(std::move(cfg))
{
  const Config& c = cfg_;
  widths_ = c.tuple_bits_per_ram.empty() ? std::vector<unsigned>(c.rams, c.tuple_bits) : c.tuple_bits_per_ram;
  if (c.rams == 0 || widths_.size() != c.rams)
    throw std::runtime_error("geometry: rams must be > 0 and match tuple_bits_per_ram");
  uint64_t entries = 0;
  for (unsigned w : widths_) {
    if (w == 0 || w > 24)
      throw std::runtime_error("geometry: tuple bits must be 1..24");
    base_.push_back(static_cast<std::size_t>(entries));
    entries += uint64_t{1} << w;
  }
  if (entries > (uint64_t{1} << 27))
    throw std::runtime_error("geometry: more than 2^27 RAM entries");
  if (c.counter_bits == 0 || c.counter_bits > 16 || (c.mode == Mode::Signed && c.counter_bits < 2))
    throw std::runtime_error("geometry: counter bits out of range");
  if (c.ghr_length == 0 || c.gphr_bits_per_branch == 0 || c.gphr_bits_per_branch >= 64 || c.gphr_length == 0 || c.gphr_length > 64)
    throw std::runtime_error("history lengths out of range");
  for (const auto& l : c.lhts)
    if (l.entries == 0 || (l.entries & (l.entries - 1)) != 0 || l.length == 0 || l.length > 64)
      throw std::runtime_error("local history tables need power-of-two entries and length 1..64");
  auto pow2 = [](unsigned v) { return v != 0 && (v & (v - 1)) == 0; };
  if ((c.default_rule == DefaultRule::BiasTable || c.default_rule == DefaultRule::LastOutcome) && !pow2(c.default_table_entries))
    throw std::runtime_error("default_entries must be a power of two");
  if (c.fallback == Fallback::Chooser && !pow2(c.fallback_entries))
    throw std::runtime_error("fallback entries must be a power of two");
  max_count_ = static_cast<uint32_t>(mask_bits(c.counter_bits));
  signed_max_ = (1 << (c.counter_bits - 1)) - 1;
  signed_min_ = -(1 << (c.counter_bits - 1));

  for (const auto& s : c.pool) {
    if (s.width == 0 || s.width > 64)
      throw std::runtime_error("source width must be 1..64");
    if (MPP_SOURCES.count(s.source))
      uses_mpp_ = true;
  }
  for (HistoryState* h : {&spec_, &real_}) {
    h->ghr.assign((c.ghr_length + 63) / 64, 0);
    h->recency.assign(c.recency_depth, 0);
    h->mod5.assign(c.mod5_depth, 0);
    h->mod7.assign(c.mod7_depth, 0);
  }
  for (const auto& l : c.lhts)
    lht_.emplace_back(l.entries, 0);

  build_sources();
  build_mapping();
  build_groups();
  if (c.mode == Mode::Signed)
    signed_.assign(static_cast<std::size_t>(entries), 0);
  else
    counters_.assign(static_cast<std::size_t>(entries) * 2, 0);
  addr_.assign(c.rams, 0);
  last_addr_.assign(c.rams, 0);
  c_t_.assign(c.rams, 0);
  c_nt_.assign(c.rams, 0);
  cand_.reserve(2 * std::size_t{c.rams} + 1);
  st_.reserve(c.rams);
  snt_.reserve(c.rams);
  if (c.default_rule == DefaultRule::BiasTable)
    default_table_.assign(c.default_table_entries, 1);
  if (c.default_rule == DefaultRule::LastOutcome)
    default_table_.assign(c.default_table_entries, 0);
  if (c.fallback == Fallback::Chooser) {
    bimodal_.assign(c.fallback_entries, 1);
    chooser_.assign(c.fallback_entries, 2);  // weakly prefer the WNN
  }
  theta_ = c.theta;
}

void Predictor::build_sources()
{
  static const std::pair<const char*, Kind> names[] = {{"pc", Kind::Pc},
                                                       {"ghr", Kind::Ghr},
                                                       {"pc_xor_ghr", Kind::PcXorGhr},
                                                       {"lhr", Kind::Lhr},
                                                       {"gphr", Kind::Gphr},
                                                       {"modhist5", Kind::Modhist5},
                                                       {"modhist7", Kind::Modhist7},
                                                       {"ghistpath", Kind::Ghistpath},
                                                       {"blurry", Kind::Blurry},
                                                       {"recencypos", Kind::Recencypos},
                                                       {"imli_nz", Kind::ImliNz},
                                                       {"imli", Kind::Imli},
                                                       {"btype", Kind::Btype},
                                                       {"folded_ghr", Kind::FoldedGhr}};
  const Config& c = cfg_;
  for (const auto& s : c.pool) {
    auto it = std::find(sources_.begin(), sources_.end(), s);
    std::size_t idx = static_cast<std::size_t>(it - sources_.begin());
    if (it == sources_.end()) {
      auto n = std::find_if(std::begin(names), std::end(names), [&](const auto& x) { return s.source == x.first; });
      if (n == std::end(names))
        throw std::runtime_error("unknown source " + s.source);
      if (s.source == "ghr" && s.offset + s.width > c.ghr_length)
        throw std::runtime_error("ghr source beyond ghr_length");
      if (s.source == "pc_xor_ghr" && s.width > c.ghr_length)
        throw std::runtime_error("pc_xor_ghr wider than ghr_length");
      if (s.source == "gphr" && s.offset + s.width > c.gphr_length)
        throw std::runtime_error("gphr source beyond path history length");
      if (s.source == "lhr" && (s.table >= c.lhts.size() || s.width > c.lhts[s.table].length))
        throw std::runtime_error("lhr source refers to a missing or shorter table");
      if (s.source == "modhist5" && s.width > c.mod5_depth)
        throw std::runtime_error("modhist5 wider than its depth");
      if (s.source == "modhist7" && s.width > c.mod7_depth)
        throw std::runtime_error("modhist7 wider than its depth");
      if ((s.source == "ghistpath" || s.source == "blurry") && s.ghr_bits > c.ghr_length)
        throw std::runtime_error("ghr_bits beyond ghr_length");
      if (s.source == "folded_ghr" && (s.length == 0 || s.offset + s.length > c.ghr_length))
        throw std::runtime_error("folded_ghr needs 0 < offset + length <= ghr_length");
      if (s.source == "btype" && s.width > 3)
        throw std::runtime_error("btype has 3 bits");
      sources_.push_back(s);
      kinds_.push_back(n->second);
    }
    if (sources_.size() > std::numeric_limits<uint16_t>::max())
      throw std::runtime_error("too many sources");
    for (unsigned r = 0; r < s.copies; ++r)
      for (unsigned b = 0; b < s.width; ++b)
        pool_positions_.emplace_back(static_cast<uint16_t>(idx), static_cast<uint8_t>(b));
  }
  if (pool_positions_.empty())
    throw std::runtime_error("empty input pool");
  words_.assign(sources_.size(), 0);
}

void Predictor::build_mapping()
{
  const Config& c = cfg_;
  mapping_.assign(c.rams, {});
  for (uint32_t i = 0; i < c.rams; ++i)
    mapping_[i].assign(widths_[i], 0);
  std::mt19937 rng(c.seed);

  // first pool position of each pool entry (copy 0)
  std::vector<int> entry_start;
  {
    int p = 0;
    for (const auto& s : c.pool) {
      entry_start.push_back(p);
      p += static_cast<int>(s.copies * s.width);
    }
  }
  auto same_bit = [&](int a, int b) { return pool_positions_[static_cast<std::size_t>(a)] == pool_positions_[static_cast<std::size_t>(b)]; };
  auto in_tuple = [&](const std::vector<int>& t, std::size_t filled, int pos) {
    for (std::size_t k = 0; k < filled; ++k)
      if (same_bit(t[k], pos))
        return true;
    return false;
  };

  if (c.mapping == "flat_shuffle") {
    // Identical to branch/wisard/wisard.cc: shuffle all pool positions with mt19937(seed),
    // deal them out in order, and reshuffle the same vector when it runs out.
    const int pool = static_cast<int>(pool_positions_.size());
    std::vector<int> input_indices(pool_positions_.size());
    std::iota(input_indices.begin(), input_indices.end(), 0);
    std::shuffle(input_indices.begin(), input_indices.end(), rng);
    int idx_ptr = 0;
    for (uint32_t i = 0; i < c.rams; ++i) {
      for (uint32_t b = 0; b < widths_[i]; ++b) {
        if (idx_ptr >= pool) {
          std::shuffle(input_indices.begin(), input_indices.end(), rng);
          idx_ptr = 0;
        }
        mapping_[i][b] = input_indices[static_cast<std::size_t>(idx_ptr++)];
      }
    }
  } else if (c.mapping == "weighted") {
    // Each tuple bit: pick a pool entry with probability proportional to its weight, then one of
    // its bits uniformly; never repeat an underlying bit within a tuple (unless impossible).
    std::vector<double> w;
    for (const auto& s : c.pool)
      w.push_back(s.weight);
    std::discrete_distribution<int> pick_entry(w.begin(), w.end());
    for (uint32_t i = 0; i < c.rams; ++i) {
      for (uint32_t b = 0; b < widths_[i]; ++b) {
        int pos = 0;
        for (int attempt = 0; attempt < 1000; ++attempt) {
          const int e = pick_entry(rng);
          std::uniform_int_distribution<unsigned> pick_bit(0, c.pool[static_cast<std::size_t>(e)].width - 1);
          pos = entry_start[static_cast<std::size_t>(e)] + static_cast<int>(pick_bit(rng));
          if (!in_tuple(mapping_[i], b, pos))
            break;
        }
        mapping_[i][b] = pos;
      }
    }
  } else if (c.mapping == "coverage") {
    // Every distinct underlying bit is used once before any is reused (a weighted random order:
    // Efraimidis-Spirakis keys u^(1/w)), and no bit repeats within a tuple.
    std::vector<std::pair<int, double>> items;  // (pool position of copy 0, weight)
    std::set<std::pair<uint16_t, uint8_t>> seen;
    for (std::size_t e = 0; e < c.pool.size(); ++e)
      for (unsigned b = 0; b < c.pool[e].width; ++b) {
        int pos = entry_start[e] + static_cast<int>(b);
        if (seen.insert(pool_positions_[static_cast<std::size_t>(pos)]).second)
          items.emplace_back(pos, c.pool[e].weight);
      }
    std::uniform_real_distribution<double> u01(0.0, 1.0);
    std::vector<int> order;
    auto refill = [&]() {
      std::vector<std::pair<double, int>> keyed;
      for (const auto& [pos, weight] : items)
        keyed.emplace_back(-std::log(1.0 - u01(rng)) / std::max(weight, 1e-12), pos);  // ascending exponential key == descending u^(1/w)
      std::sort(keyed.begin(), keyed.end());
      order.clear();
      for (const auto& kv : keyed)
        order.push_back(kv.second);
    };
    refill();
    std::size_t next = 0;
    for (uint32_t i = 0; i < c.rams; ++i) {
      for (uint32_t b = 0; b < widths_[i]; ++b) {
        if (next >= order.size()) {
          refill();
          next = 0;
        }
        std::size_t k = next;
        while (k < order.size() && in_tuple(mapping_[i], b, order[k]))
          ++k;
        if (k == order.size())
          k = next;  // every remaining bit is already in this tuple: accept a repeat
        std::swap(order[next], order[k]);
        mapping_[i][b] = order[next++];
      }
    }
  } else if (c.mapping == "structured") {
    // RAM i reads the lowest pc_bits PC bits plus history bits spread evenly over a window of the
    // most recent W_i global outcomes, W_i growing geometrically from window_min to window_max.
    auto entry_of = [&](const char* source) {
      for (std::size_t e = 0; e < c.pool.size(); ++e)
        if (c.pool[e].source == source && c.pool[e].offset == 0)
          return static_cast<int>(e);
      throw std::runtime_error(std::string("structured mapping needs a pool entry with source ") + source + " at offset 0");
    };
    const int pc_e = entry_of("pc"), ghr_e = entry_of("ghr");
    const unsigned ghr_w = c.pool[static_cast<std::size_t>(ghr_e)].width, pc_w = c.pool[static_cast<std::size_t>(pc_e)].width;
    for (uint32_t i = 0; i < c.rams; ++i) {
      const unsigned n = widths_[i];
      const unsigned p = std::min({c.structured_pc_bits, n, pc_w});
      const double frac = c.rams > 1 ? static_cast<double>(i) / (c.rams - 1) : 0.0;
      unsigned window = static_cast<unsigned>(std::lround(c.structured_window_min * std::pow(static_cast<double>(c.structured_window_max) / c.structured_window_min, frac)));
      window = std::clamp(window, 1u, ghr_w);
      const unsigned m = n - p;
      unsigned filled = 0;
      for (unsigned b = 0; b < p; ++b)
        mapping_[i][filled++] = entry_start[static_cast<std::size_t>(pc_e)] + static_cast<int>(b);
      for (unsigned k = 0; k < m && k < window; ++k) {
        // evenly spaced over the window when it is wider than the history share, else every bit of it
        const unsigned bit = m < window ? static_cast<unsigned>((static_cast<uint64_t>(k) * window) / m) : k;
        mapping_[i][filled++] = entry_start[static_cast<std::size_t>(ghr_e)] + static_cast<int>(bit);
      }
      for (unsigned b = p; filled < n; ++b)  // window narrower than the history share: use more PC bits
        mapping_[i][filled++] = entry_start[static_cast<std::size_t>(pc_e)] + static_cast<int>(b % pc_w);
    }
  } else {
    throw std::runtime_error("unknown mapping strategy " + c.mapping);
  }
}

void Predictor::build_groups()
{
  ram_groups_.assign(cfg_.rams, {});
  for (uint32_t i = 0; i < cfg_.rams; ++i) {
    std::vector<Group> groups;
    for (int pos : mapping_[i]) {
      auto [src, bit] = pool_positions_[static_cast<std::size_t>(pos)];
      auto g = std::find_if(groups.begin(), groups.end(), [&](const Group& x) { return x.source == src; });
      if (g == groups.end()) {
        groups.push_back(Group{src, 0, 0});
        g = groups.end() - 1;
      }
      g->mask |= uint64_t{1} << bit;  // a repeated bit collapses onto itself (see header note)
    }
    unsigned shift = 0;
    for (auto& g : groups) {
      g.shift = static_cast<uint8_t>(shift);
      shift += static_cast<unsigned>(__builtin_popcountll(g.mask));
    }
    ram_groups_[i] = std::move(groups);
  }
}

uint64_t Predictor::ghr_bits(const std::vector<uint64_t>& ghr, unsigned offset, unsigned width) const
{
  unsigned w = offset / 64, b = offset % 64;
  uint64_t v = ghr[w] >> b;
  if (b != 0 && w + 1 < ghr.size())
    v |= ghr[w + 1] << (64 - b);
  return v & mask_bits(width);
}

void Predictor::push_ghr(std::vector<uint64_t>& ghr, bool bit) const
{
  for (std::size_t w = ghr.size(); w-- > 1;)
    ghr[w] = (ghr[w] << 1) | (ghr[w - 1] >> 63);
  ghr[0] = (ghr[0] << 1) | (bit ? 1u : 0u);
  unsigned top = cfg_.ghr_length % 64;
  if (top != 0)
    ghr.back() &= mask_bits(top);
}

uint32_t Predictor::recency_position(const HistoryState& h, uint32_t pc_shifted) const
{
  for (uint32_t i = 0; i < cfg_.recency_depth; ++i)
    if (h.recency[i] == pc_shifted)
      return (i * cfg_.recency_buckets) / cfg_.recency_depth;
  return cfg_.recency_buckets - 1;
}

void Predictor::insert_recency(std::vector<uint32_t>& stack, uint32_t pc_shifted)
{
  std::size_t hit = 0;
  for (; hit < stack.size(); ++hit)
    if (stack[hit] == pc_shifted)
      break;
  if (hit == stack.size()) {
    hit = stack.size() - 1;
    stack[hit] = pc_shifted;
  }
  const uint32_t entry = stack[hit];
  for (std::size_t i = hit; i > 0; --i)
    stack[i] = stack[i - 1];
  stack[0] = entry;
}

void Predictor::push_history(std::vector<uint8_t>& hist, uint8_t value)
{
  for (std::size_t i = hist.size(); i-- > 1;)
    hist[i] = hist[i - 1];
  if (!hist.empty())
    hist[0] = value;
}

std::size_t Predictor::side_index(uint64_t ip, std::size_t entries) { return static_cast<std::size_t>((ip ^ (ip >> 13)) & (entries - 1)); }

bool Predictor::passes(TypeFilter f, bool conditional, bool taken)
{
  switch (f) {
  case TypeFilter::All:
    return true;
  case TypeFilter::Conditional:
    return conditional;
  case TypeFilter::Taken:
    return taken;
  }
  return true;
}

void Predictor::compute_words(uint64_t ip, uint8_t branch_type, const HistoryState& h)
{
  const uint32_t pc_shifted = static_cast<uint32_t>((ip >> 2) & 0xFFFFFF);
  for (std::size_t k = 0; k < sources_.size(); ++k) {
    const SourceSpec& s = sources_[k];
    const uint64_t m = mask_bits(s.width);
    uint64_t v = 0;
    switch (kinds_[k]) {
    case Kind::Pc:
      v = (ip >> s.shift) & m;
      break;
    case Kind::Ghr:
      v = ghr_bits(h.ghr, s.offset, s.width);
      break;
    case Kind::PcXorGhr:
      v = ((ip >> s.shift) & m) ^ ghr_bits(h.ghr, 0, s.width);
      break;
    case Kind::Lhr: {
      const auto& t = lht_[s.table];
      v = t[ip & (t.size() - 1)] & m;
      break;
    }
    case Kind::Gphr:
      v = (gphr_ >> s.offset) & m;
      break;
    case Kind::Modhist5:
    case Kind::Modhist7: {
      const auto& hist = kinds_[k] == Kind::Modhist5 ? h.mod5 : h.mod7;
      for (unsigned i = 0; i < s.width; ++i)
        v |= uint64_t{hist[i] & 1u} << i;
      break;
    }
    case Kind::Ghistpath: {
      uint64_t g = ghr_bits(h.ghr, 0, s.ghr_bits ? s.ghr_bits : std::min(cfg_.ghr_length, 64u));
      v = ((g << s.shift) ^ (ip & mask_bits(s.pc_bits))) & m;
      break;
    }
    case Kind::Blurry: {
      uint64_t g = ghr_bits(h.ghr, 0, s.ghr_bits ? s.ghr_bits : std::min(cfg_.ghr_length, 64u));
      v = (g >> s.shift) & m;
      break;
    }
    case Kind::Recencypos:
      v = recency_position(h, pc_shifted) & m;
      break;
    case Kind::ImliNz:
      v = imli_ != 0 ? 1 : 0;
      break;
    case Kind::Imli:
      v = imli_ & m;
      break;
    case Kind::Btype:
      v = branch_type & m;
      break;
    case Kind::FoldedGhr:
      for (unsigned done = 0; done < s.length; done += s.width)
        v ^= ghr_bits(h.ghr, s.offset + done, std::min(s.width, s.length - done));
      break;
    }
    words_[k] = v;
  }
}

bool Predictor::default_prediction()
{
  switch (cfg_.default_rule) {
  case DefaultRule::NotTaken:
    return false;
  case DefaultRule::Taken:
    return true;
  case DefaultRule::BiasTable:
    return default_table_[side_index(last_ip_, default_table_.size())] >= 2;
  case DefaultRule::LastOutcome:
    return default_table_[side_index(last_ip_, default_table_.size())] != 0;
  }
  return false;
}

bool Predictor::decide_discriminators(const uint32_t* addr)
{
  ++stats_.predictions;
  const uint32_t thr = cfg_.base_threshold;
  uint32_t r_t = 0, r_nt = 0;
  for (uint32_t i = 0; i < cfg_.rams; ++i) {
    const std::size_t idx = (base_[i] + addr[i]) * 2;
    c_nt_[i] = counters_[idx];
    c_t_[i] = counters_[idx + 1];
    r_nt += c_nt_[i] > thr;
    r_t += c_t_[i] > thr;
  }
  last_margin_ = r_t > r_nt ? r_t - r_nt : r_nt - r_t;
  if (r_t != r_nt)
    return r_t > r_nt;
  ++stats_.ties;
  return resolve_tie();
}

bool Predictor::resolve_tie()
{
  const uint32_t thr = cfg_.base_threshold;
  auto record_steps = [&](uint64_t steps) {
    stats_.bleach_steps_sum += steps;
    stats_.bleach_steps_max = std::max(stats_.bleach_steps_max, steps);
  };
  auto unresolved = [&]() {
    ++stats_.unresolved_ties;
    return default_prediction();
  };
  if (cfg_.tie == TieRule::SumCounters) {
    uint64_t s_t = std::accumulate(c_t_.begin(), c_t_.end(), uint64_t{0});
    uint64_t s_nt = std::accumulate(c_nt_.begin(), c_nt_.end(), uint64_t{0});
    return s_t != s_nt ? s_t > s_nt : unresolved();
  }
  if (cfg_.tie == TieRule::MaxCounter) {
    uint16_t m_t = *std::max_element(c_t_.begin(), c_t_.end()), m_nt = *std::max_element(c_nt_.begin(), c_nt_.end());
    return m_t != m_nt ? m_t > m_nt : unresolved();
  }

  // Bleaching: raise the threshold b from thr+1 and stop at the first b where #(c_t > b) != #(c_nt > b).
  // With both arrays sorted, each count is one binary search.
  st_.assign(c_t_.begin(), c_t_.end());
  snt_.assign(c_nt_.begin(), c_nt_.end());
  std::sort(st_.begin(), st_.end());
  std::sort(snt_.begin(), snt_.end());
  auto above = [](const std::vector<uint16_t>& v, uint32_t b) { return static_cast<uint32_t>(v.end() - std::upper_bound(v.begin(), v.end(), b)); };
  const uint32_t top = std::max(st_.empty() ? 0u : st_.back(), snt_.empty() ? 0u : snt_.back());

  if (cfg_.tie == TieRule::BleachLinear) {
    // The original tests b = thr+1, thr+2, ..., max_count. Both counts are constant between
    // consecutive stored counter values, so testing thr+1 and every stored value above it suffices.
    std::vector<uint32_t>& cand = cand_;
    cand.clear();
    cand.push_back(thr + 1);
    for (uint16_t v : st_)
      if (v > thr + 1)
        cand.push_back(v);
    for (uint16_t v : snt_)
      if (v > thr + 1)
        cand.push_back(v);
    std::sort(cand.begin(), cand.end());
    cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
    for (uint32_t b : cand) {
      if (b > max_count_)
        break;
      const uint32_t t = above(st_, b), nt = above(snt_, b);
      if (t != nt) {
        record_steps(b - thr);
        return t > nt;
      }
    }
    record_steps(max_count_ > thr ? max_count_ - thr : 0);
    return unresolved();
  }

  // Coarser bleaching schedules: b = thr+1, thr+3, ... (step 2) or thr+1, thr+2, thr+4, ... (doubling).
  uint64_t steps = 0;
  for (uint32_t k = 1, b = thr + 1; b <= max_count_ && b <= top; ++k) {
    ++steps;
    const uint32_t t = above(st_, b), nt = above(snt_, b);
    if (t != nt) {
      record_steps(steps);
      return t > nt;
    }
    b = cfg_.tie == TieRule::BleachStep2 ? b + 2 : thr + (1u << k);
  }
  record_steps(steps);
  return unresolved();
}

bool Predictor::decide_signed(const uint32_t* addr)
{
  ++stats_.predictions;
  int64_t sum = 0;
  for (uint32_t i = 0; i < cfg_.rams; ++i)
    sum += signed_[base_[i] + addr[i]];
  last_margin_ = static_cast<uint32_t>(sum < 0 ? -sum : sum);
  if (sum != 0)
    return sum > 0;
  ++stats_.ties;
  ++stats_.unresolved_ties;
  return default_prediction();
}

bool Predictor::predict(uint64_t ip, uint8_t branch_type)
{
  const bool spec = cfg_.history == HistoryPolicy::SpecEveryCall;
  last_ip_ = ip;
  compute_words(ip, branch_type, spec ? spec_ : real_);
  for (uint32_t i = 0; i < cfg_.rams; ++i) {
    uint32_t a = 0;
    for (const Group& g : ram_groups_[i])
      a |= static_cast<uint32_t>(pext64(words_[g.source], g.mask) << g.shift);
    addr_[i] = a;
  }
  const bool wnn = cfg_.mode == Mode::Signed ? decide_signed(addr_.data()) : decide_discriminators(addr_.data());
  bool pred = wnn;
  last_wnn_pred_ = wnn;
  if (cfg_.fallback == Fallback::Chooser) {
    const std::size_t k = side_index(ip, bimodal_.size());
    last_bimodal_pred_ = bimodal_[k] >= 2;
    if (chooser_[k] < 2) {
      pred = last_bimodal_pred_;
      ++stats_.chooser_used_bimodal;
    }
  }
  std::swap(addr_, last_addr_);
  last_pred_ = pred;

  if (spec) {
    // The original design shifts its own prediction into the speculative history on every call,
    // and ChampSim calls predict_branch for every instruction (finding 1 in CLAUDE.md).
    const uint32_t pc_shifted = static_cast<uint32_t>((ip >> 2) & 0xFFFFFF);
    if (uses_mpp_) {
      insert_recency(spec_.recency, pc_shifted);
      if (pc_shifted % 5u == 0)
        push_history(spec_.mod5, pred ? 1 : 0);
      if (pc_shifted % 7u == 0)
        push_history(spec_.mod7, pred ? 1 : 0);
    }
    push_ghr(spec_.ghr, pred);
  }
  return pred;
}

void Predictor::decay_all()
{
  switch (cfg_.decay) {
  case DecayMethod::Halve:
    for (auto& v : counters_)
      v = static_cast<uint16_t>(v >> 1);
    for (auto& v : signed_)
      v = static_cast<int16_t>(v / 2);
    break;
  case DecayMethod::Subtract1:
    for (auto& v : counters_)
      if (v > 0)
        --v;
    for (auto& v : signed_)
      v = static_cast<int16_t>(v > 0 ? v - 1 : (v < 0 ? v + 1 : 0));
    break;
  case DecayMethod::None:
    break;
  }
}

void Predictor::train(bool taken)
{
  ++stats_.trainings;
  const Config& c = cfg_;
  for (uint32_t i = 0; i < c.rams; ++i) {
    const std::size_t entry = base_[i] + last_addr_[i];
    if (c.mode == Mode::Signed) {
      int16_t& v = signed_[entry];
      if (taken && v < signed_max_)
        ++v;
      if (!taken && v > signed_min_)
        --v;
      continue;
    }
    uint16_t& win = counters_[entry * 2 + (taken ? 1 : 0)];
    uint16_t& lose = counters_[entry * 2 + (taken ? 0 : 1)];
    if (win >= max_count_ && c.saturation == Saturation::HalvePair) {
      win = static_cast<uint16_t>(win >> 1);
      lose = static_cast<uint16_t>(lose >> 1);
    }
    if (win < max_count_)
      ++win;
    if (c.train_side == TrainSide::Two && lose > 0)
      --lose;
  }
}

void Predictor::update(uint64_t ip, uint64_t target, bool taken, uint8_t branch_type)
{
  const Config& c = cfg_;
  const bool conditional = branch_type == BRANCH_CONDITIONAL_TYPE;

  if (c.decay_interval != 0 && ++since_decay_ >= c.decay_interval) {
    decay_all();
    since_decay_ = 0;
  }

  if (passes(c.train_types, conditional, taken)) {
    const bool wnn_wrong = last_wnn_pred_ != taken;
    bool gate = true;
    if (c.train_gate == TrainGate::Mispredict)
      gate = wnn_wrong;
    else if (c.train_gate == TrainGate::Margin)
      gate = wnn_wrong || last_margin_ <= c.theta;
    else if (c.train_gate == TrainGate::Adaptive) {
      gate = wnn_wrong || last_margin_ <= theta_;
      // Adaptive threshold: raise it after repeated mispredictions, lower it after repeated
      // low-margin correct predictions.
      if (wnn_wrong) {
        if (++tc_ >= 63) {
          ++theta_;
          tc_ = 0;
        }
      } else if (last_margin_ <= theta_) {
        if (--tc_ <= -64) {
          if (theta_ > 0)
            --theta_;
          tc_ = 0;
        }
      }
    }
    if (gate)
      train(taken);
    if (c.fallback == Fallback::Chooser) {
      const std::size_t k = side_index(ip, bimodal_.size());
      if (last_wnn_pred_ != last_bimodal_pred_) {
        uint8_t& ch = chooser_[k];
        if (!wnn_wrong && ch < 3)
          ++ch;
        if (wnn_wrong && ch > 0)
          --ch;
      }
      uint8_t& bm = bimodal_[k];
      if (taken && bm < 3)
        ++bm;
      if (!taken && bm > 0)
        --bm;
    }
    if (c.default_rule == DefaultRule::BiasTable) {
      uint8_t& v = default_table_[side_index(ip, default_table_.size())];
      if (taken && v < 3)
        ++v;
      if (!taken && v > 0)
        --v;
    } else if (c.default_rule == DefaultRule::LastOutcome) {
      default_table_[side_index(ip, default_table_.size())] = taken ? 1 : 0;
    }
  }

  if (passes(c.history_types, conditional, taken)) {
    const uint32_t pc_shifted = static_cast<uint32_t>((ip >> 2) & 0xFFFFFF);
    push_ghr(real_.ghr, taken);
    if (uses_mpp_) {
      insert_recency(real_.recency, pc_shifted);
      if (pc_shifted % 5u == 0)
        push_history(real_.mod5, taken ? 1 : 0);
      if (pc_shifted % 7u == 0)
        push_history(real_.mod7, taken ? 1 : 0);
    }
    if (c.history == HistoryPolicy::SpecEveryCall && last_pred_ != taken)
      spec_ = real_;
    if (uses_mpp_) {
      const bool backward = target < ip;  // as in wisard_mpp_v2; for not-taken branches target is 0
      if (conditional && backward) {
        if (taken) {
          if (imli_ < mask_bits(c.imli_bits))
            ++imli_;
        } else {
          imli_ = 0;
        }
      } else if (!taken) {
        imli_ = 0;
      }
    }
    for (std::size_t k = 0; k < lht_.size(); ++k) {
      auto& slot = lht_[k][ip & (lht_[k].size() - 1)];
      slot = ((slot << 1) | (taken ? 1u : 0u)) & mask_bits(c.lhts[k].length);
    }
  }
  if (passes(c.path_types, conditional, taken))
    gphr_ = ((gphr_ << c.gphr_bits_per_branch) ^ (ip & mask_bits(c.gphr_bits_per_branch))) & mask_bits(c.gphr_length);
}

Storage Predictor::storage() const
{
  const Config& c = cfg_;
  const uint64_t copies = c.history == HistoryPolicy::SpecEveryCall ? 2 : 1;
  Storage s;
  uint64_t entries = 0;
  for (unsigned w : widths_)
    entries += uint64_t{1} << w;
  s.discriminator_bits = (c.mode == Mode::Signed ? 1 : 2) * entries * c.counter_bits;
  for (const auto& l : c.lhts)
    s.lht_bits += uint64_t{l.entries} * l.length;
  s.global_history_bits = uint64_t{c.ghr_length} * copies;
  s.path_history_bits = c.gphr_length;
  if (uses_mpp_)
    s.mpp_state_bits = (uint64_t{c.recency_depth} * 24 + c.mod5_depth + c.mod7_depth) * copies + c.imli_bits;
  if (c.default_rule == DefaultRule::BiasTable)
    s.side_table_bits += uint64_t{c.default_table_entries} * 2;
  if (c.default_rule == DefaultRule::LastOutcome)
    s.side_table_bits += c.default_table_entries;
  if (c.fallback == Fallback::Chooser)
    s.side_table_bits += uint64_t{c.fallback_entries} * 4;  // 2-bit bimodal + 2-bit chooser
  if (c.train_gate == TrainGate::Adaptive)
    s.side_table_bits += 8 + 7;  // threshold and its training counter
  return s;
}

}  // namespace wnn_core
