#include "wnn_core.h"

#include <algorithm>
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

TypeFilter parse_filter(const std::string& s)
{
  if (s == "all")
    return TypeFilter::All;
  if (s == "conditional")
    return TypeFilter::Conditional;
  throw std::runtime_error("unknown branch-type filter " + s);
}

const std::set<std::string> MPP_SOURCES = {"modhist5", "modhist7", "recencypos", "imli_nz", "imli"};
const std::set<std::string> ALL_SOURCES = {"pc", "ghr", "pc_xor_ghr", "lhr", "gphr", "modhist5", "modhist7", "ghistpath", "blurry", "recencypos", "imli_nz", "imli"};
}  // namespace

bool SourceSpec::operator==(const SourceSpec& o) const
{
  return source == o.source && width == o.width && shift == o.shift && offset == o.offset && table == o.table && ghr_bits == o.ghr_bits && pc_bits == o.pc_bits;
}

Config Config::from_json_text(const std::string& text)
{
  json j = json::parse(text);
  check_keys(j, {"name", "description", "geometry", "history", "lhts", "path_history", "mpp_state", "pool", "mapping", "predict", "train"}, "config");
  Config c;
  c.name = get_or<std::string>(j, "name", c.name);
  if (j.contains("geometry")) {
    const auto& g = j.at("geometry");
    check_keys(g, {"rams", "tuple_bits", "counter_bits"}, "geometry");
    c.rams = get_or(g, "rams", c.rams);
    c.tuple_bits = get_or(g, "tuple_bits", c.tuple_bits);
    c.counter_bits = get_or(g, "counter_bits", c.counter_bits);
  }
  if (j.contains("history")) {
    const auto& h = j.at("history");
    check_keys(h, {"policy", "types", "ghr_length"}, "history");
    std::string p = get_or<std::string>(h, "policy", "spec_every_call");
    if (p == "spec_every_call")
      c.history = HistoryPolicy::SpecEveryCall;
    else if (p == "update_only")
      c.history = HistoryPolicy::UpdateOnly;
    else
      throw std::runtime_error("unknown history policy " + p);
    c.history_types = parse_filter(get_or<std::string>(h, "types", "all"));
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
    check_keys(s, {"source", "width", "shift", "offset", "table", "ghr_bits", "pc_bits", "copies", "weight"}, "pool[]");
    SourceSpec sp;
    sp.source = s.at("source").get<std::string>();
    sp.width = s.at("width").get<unsigned>();
    sp.shift = get_or(s, "shift", 0u);
    sp.offset = get_or(s, "offset", 0u);
    sp.table = get_or(s, "table", 0u);
    sp.ghr_bits = get_or(s, "ghr_bits", 0u);
    sp.pc_bits = get_or(s, "pc_bits", 24u);
    sp.copies = get_or(s, "copies", 1u);
    sp.weight = get_or(s, "weight", 1.0);
    c.pool.push_back(sp);
  }
  if (j.contains("mapping")) {
    const auto& m = j.at("mapping");
    check_keys(m, {"strategy", "seed"}, "mapping");
    c.mapping = get_or<std::string>(m, "strategy", c.mapping);
    c.seed = get_or(m, "seed", c.seed);
  }
  if (j.contains("predict")) {
    const auto& p = j.at("predict");
    check_keys(p, {"threshold", "tie", "default"}, "predict");
    c.base_threshold = get_or(p, "threshold", c.base_threshold);
    std::string t = get_or<std::string>(p, "tie", "bleach_linear");
    if (t == "bleach_linear")
      c.tie = TieRule::BleachLinear;
    else if (t == "sum_counters")
      c.tie = TieRule::SumCounters;
    else
      throw std::runtime_error("unknown tie rule " + t);
    std::string d = get_or<std::string>(p, "default", "not_taken");
    if (d != "not_taken" && d != "taken")
      throw std::runtime_error("unknown default " + d);
    c.default_taken = d == "taken";
  }
  if (j.contains("train")) {
    const auto& t = j.at("train");
    check_keys(t, {"side", "gate", "theta", "types", "decay_interval", "decay"}, "train");
    std::string side = get_or<std::string>(t, "side", "one");
    if (side == "one")
      c.train_side = TrainSide::One;
    else if (side == "two")
      c.train_side = TrainSide::Two;
    else
      throw std::runtime_error("unknown train side " + side);
    std::string gate = get_or<std::string>(t, "gate", "always");
    if (gate == "always")
      c.train_gate = TrainGate::Always;
    else if (gate == "mispredict")
      c.train_gate = TrainGate::Mispredict;
    else if (gate == "margin")
      c.train_gate = TrainGate::Margin;
    else
      throw std::runtime_error("unknown train gate " + gate);
    c.theta = get_or(t, "theta", c.theta);
    c.train_types = parse_filter(get_or<std::string>(t, "types", "all"));
    c.decay_interval = get_or(t, "decay_interval", c.decay_interval);
    std::string d = get_or<std::string>(t, "decay", "halve");
    if (d == "halve")
      c.decay = DecayMethod::Halve;
    else if (d == "none")
      c.decay = DecayMethod::None;
    else
      throw std::runtime_error("unknown decay " + d);
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
  if (c.rams == 0 || c.tuple_bits == 0 || c.tuple_bits > 24 || c.counter_bits == 0 || c.counter_bits > 16)
    throw std::runtime_error("geometry out of range");
  if ((uint64_t{c.rams} << c.tuple_bits) > (uint64_t{1} << 27))
    throw std::runtime_error("rams * 2^tuple_bits exceeds 2^27 entries");
  if (c.ghr_length == 0 || c.gphr_bits_per_branch == 0 || c.gphr_bits_per_branch >= 64 || c.gphr_length == 0 || c.gphr_length > 64)
    throw std::runtime_error("history lengths out of range");
  for (const auto& l : c.lhts)
    if (l.entries == 0 || (l.entries & (l.entries - 1)) != 0 || l.length == 0 || l.length > 64)
      throw std::runtime_error("local history tables need power-of-two entries and length 1..64");
  max_count_ = static_cast<uint32_t>(mask_bits(c.counter_bits));

  for (const auto& s : c.pool) {
    if (!ALL_SOURCES.count(s.source))
      throw std::runtime_error("unknown source " + s.source);
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
  counters_.assign((std::size_t{c.rams} << c.tuple_bits) * 2, 0);
  addr_.assign(c.rams, 0);
  last_addr_.assign(c.rams, 0);
  c_t_.assign(c.rams, 0);
  c_nt_.assign(c.rams, 0);
}

void Predictor::build_sources()
{
  for (const auto& s : cfg_.pool) {
    auto it = std::find(sources_.begin(), sources_.end(), s);
    std::size_t idx = static_cast<std::size_t>(it - sources_.begin());
    if (it == sources_.end()) {
      const Config& c = cfg_;
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
      sources_.push_back(s);
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
  static const std::pair<const char*, Kind> names[] = {{"pc", Kind::Pc}, {"ghr", Kind::Ghr}, {"pc_xor_ghr", Kind::PcXorGhr}, {"lhr", Kind::Lhr},
      {"gphr", Kind::Gphr}, {"modhist5", Kind::Modhist5}, {"modhist7", Kind::Modhist7}, {"ghistpath", Kind::Ghistpath}, {"blurry", Kind::Blurry},
      {"recencypos", Kind::Recencypos}, {"imli_nz", Kind::ImliNz}, {"imli", Kind::Imli}};
  for (const auto& src : sources_)
    for (const auto& [name, kind] : names)
      if (src.source == name)
        kinds_.push_back(kind);
}

void Predictor::build_mapping()
{
  const Config& c = cfg_;
  mapping_.assign(c.rams, std::vector<int>(c.tuple_bits));
  if (c.mapping == "flat_shuffle") {
    // Identical to branch/wisard/wisard.cc: shuffle all pool positions with mt19937(seed),
    // deal them out in order, and reshuffle the same vector when it runs out.
    const int pool = static_cast<int>(pool_positions_.size());
    std::vector<int> input_indices(pool_positions_.size());
    std::iota(input_indices.begin(), input_indices.end(), 0);
    std::mt19937 rng(c.seed);
    std::shuffle(input_indices.begin(), input_indices.end(), rng);
    int idx_ptr = 0;
    for (uint32_t i = 0; i < c.rams; ++i) {
      for (uint32_t b = 0; b < c.tuple_bits; ++b) {
        if (idx_ptr >= pool) {
          std::shuffle(input_indices.begin(), input_indices.end(), rng);
          idx_ptr = 0;
        }
        mapping_[i][b] = input_indices[static_cast<std::size_t>(idx_ptr++)];
      }
    }
  } else {
    throw std::runtime_error("unknown mapping strategy " + c.mapping);
  }

  ram_groups_.assign(c.rams, {});
  for (uint32_t i = 0; i < c.rams; ++i) {
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

void Predictor::compute_words(uint64_t ip, const HistoryState& h)
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
    }
    words_[k] = v;
  }
}

bool Predictor::decide(const uint32_t* addr)
{
  ++stats_.predictions;
  const unsigned n = cfg_.tuple_bits;
  const uint32_t thr = cfg_.base_threshold;
  uint32_t r_t = 0, r_nt = 0;
  for (uint32_t i = 0; i < cfg_.rams; ++i) {
    const std::size_t idx = ((std::size_t{i} << n) | addr[i]) * 2;
    c_nt_[i] = counters_[idx];
    c_t_[i] = counters_[idx + 1];
    r_nt += c_nt_[i] > thr;
    r_t += c_t_[i] > thr;
  }
  last_margin_ = r_t > r_nt ? r_t - r_nt : r_nt - r_t;
  if (r_t != r_nt)
    return r_t > r_nt;

  ++stats_.ties;
  if (cfg_.tie == TieRule::SumCounters) {
    uint64_t s_t = std::accumulate(c_t_.begin(), c_t_.end(), uint64_t{0});
    uint64_t s_nt = std::accumulate(c_nt_.begin(), c_nt_.end(), uint64_t{0});
    if (s_t != s_nt)
      return s_t > s_nt;
    ++stats_.unresolved_ties;
    return cfg_.default_taken;
  }

  // Bleaching: the original raises b = thr+1, thr+2, ... up to max_count and stops at the first b
  // where #(c_t > b) != #(c_nt > b). Both counts are constant between consecutive stored counter
  // values, so it suffices to test b = thr+1 and every stored value above it, in ascending order.
  std::vector<uint32_t> cand;
  cand.reserve(2 * cfg_.rams + 1);
  cand.push_back(thr + 1);
  for (uint32_t i = 0; i < cfg_.rams; ++i) {
    if (c_t_[i] > thr + 1)
      cand.push_back(c_t_[i]);
    if (c_nt_[i] > thr + 1)
      cand.push_back(c_nt_[i]);
  }
  std::sort(cand.begin(), cand.end());
  cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
  for (uint32_t b : cand) {
    if (b > max_count_)
      break;
    uint32_t t = 0, nt = 0;
    for (uint32_t i = 0; i < cfg_.rams; ++i) {
      t += c_t_[i] > b;
      nt += c_nt_[i] > b;
    }
    if (t != nt) {
      uint64_t steps = b - thr;
      stats_.bleach_steps_sum += steps;
      stats_.bleach_steps_max = std::max(stats_.bleach_steps_max, steps);
      return t > nt;
    }
  }
  ++stats_.unresolved_ties;
  uint64_t steps = max_count_ > thr ? max_count_ - thr : 0;
  stats_.bleach_steps_sum += steps;
  stats_.bleach_steps_max = std::max(stats_.bleach_steps_max, steps);
  return cfg_.default_taken;
}

bool Predictor::predict(uint64_t ip)
{
  const bool spec = cfg_.history == HistoryPolicy::SpecEveryCall;
  compute_words(ip, spec ? spec_ : real_);
  for (uint32_t i = 0; i < cfg_.rams; ++i) {
    uint32_t a = 0;
    for (const Group& g : ram_groups_[i])
      a |= static_cast<uint32_t>(pext64(words_[g.source], g.mask) << g.shift);
    addr_[i] = a;
  }
  const bool pred = decide(addr_.data());
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
  if (cfg_.decay == DecayMethod::Halve)
    for (auto& v : counters_)
      v = static_cast<uint16_t>(v >> 1);
}

void Predictor::update(uint64_t ip, uint64_t target, bool taken, uint8_t branch_type)
{
  const Config& c = cfg_;
  const bool conditional = branch_type == BRANCH_CONDITIONAL_TYPE;

  if (c.decay_interval != 0 && ++since_decay_ >= c.decay_interval) {
    decay_all();
    since_decay_ = 0;
  }

  if (c.train_types == TypeFilter::All || conditional) {
    bool gate = true;
    if (c.train_gate == TrainGate::Mispredict)
      gate = last_pred_ != taken;
    else if (c.train_gate == TrainGate::Margin)
      gate = last_pred_ != taken || last_margin_ <= c.theta;
    if (gate) {
      ++stats_.trainings;
      const unsigned n = c.tuple_bits;
      for (uint32_t i = 0; i < c.rams; ++i) {
        const std::size_t idx = ((std::size_t{i} << n) | last_addr_[i]) * 2;
        uint16_t& win = counters_[idx + (taken ? 1 : 0)];
        if (win < max_count_)
          ++win;
        if (c.train_side == TrainSide::Two) {
          uint16_t& lose = counters_[idx + (taken ? 0 : 1)];
          if (lose > 0)
            --lose;
        }
      }
    }
  }

  if (c.history_types == TypeFilter::All || conditional) {
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
    gphr_ = ((gphr_ << c.gphr_bits_per_branch) ^ (ip & mask_bits(c.gphr_bits_per_branch))) & mask_bits(c.gphr_length);
  }
}

Storage Predictor::storage() const
{
  const Config& c = cfg_;
  const uint64_t copies = c.history == HistoryPolicy::SpecEveryCall ? 2 : 1;
  Storage s;
  s.discriminator_bits = 2ULL * c.rams * (1ULL << c.tuple_bits) * c.counter_bits;
  for (const auto& l : c.lhts)
    s.lht_bits += uint64_t{l.entries} * l.length;
  s.global_history_bits = uint64_t{c.ghr_length} * copies;
  s.path_history_bits = c.gphr_length;
  if (uses_mpp_)
    s.mpp_state_bits = (uint64_t{c.recency_depth} * 24 + c.mod5_depth + c.mod7_depth) * copies + c.imli_bits;
  return s;
}

}  // namespace wnn_core
