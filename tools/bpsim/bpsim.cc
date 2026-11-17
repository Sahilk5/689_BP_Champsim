// bpsim: replay a trace's branch stream through a ChampSim branch predictor and basic_btb,
// in exactly the order ChampSim's front end calls them (src/ooo_cpu.cc, do_predict_branch):
//   for every instruction: btb_prediction(ip); prediction = predict_branch(ip) || always_taken
//   for branches only:     [count mispredict]; update_btb(...); last_branch_result(...)
// ChampSim models no wrong path and calls the update right after the prediction, so the
// predictor's state sequence is identical to a ChampSim run. Counts can differ only at the
// warmup/measurement boundary: ChampSim switches phases when instructions retire, while the
// harness switches at the fetch index (a difference of at most the in-flight window).
//
// Modes:
//   --stream F.bst   cached branch stream (fast; requires predict() without side effects)
//   --trace T        raw trace through ChampSim's reader (every instruction; for exactness tests)
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "instruction.h"
#include "predictor_iface.h"
#include "stream_format.h"
#include "tracereader.h"

namespace
{
struct Args {
  std::string predictor, config, stream, trace, json_out, per_branch_out, dump_predictions;
  uint64_t warmup = 50'000'000, sim = 100'000'000;
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
    if (k == "--predictor")
      a.predictor = next();
    else if (k == "--config")
      a.config = next();
    else if (k == "--stream")
      a.stream = next();
    else if (k == "--trace")
      a.trace = next();
    else if (k == "--warmup")
      a.warmup = std::stoull(next());
    else if (k == "--sim")
      a.sim = std::stoull(next());
    else if (k == "--json")
      a.json_out = next();
    else if (k == "--per-branch")
      a.per_branch_out = next();
    else if (k == "--dump-predictions")
      a.dump_predictions = next();
    else
      throw std::runtime_error("unknown argument " + k);
  }
  if (a.predictor.empty() || (a.stream.empty() == a.trace.empty()))
    throw std::runtime_error("usage: bpsim --predictor P [--config C] (--stream F.bst | --trace T) [--warmup W] [--sim S] [--json OUT] "
                             "[--per-branch OUT.csv] [--dump-predictions OUT.bin]");
  return a;
}

struct TypeCount {
  uint64_t count = 0, taken = 0, mispredicts = 0;
};

struct PerBranch {
  uint64_t ip = 0, execs = 0, taken = 0, dir_mispredicts = 0;
};

class Replay
{
public:
  Replay(IPredictor& p, IBtb& b, const Args& a) : pred_(p), btb_(b), args_(a)
  {
    if (!a.dump_predictions.empty()) {
      dump_ = std::fopen(a.dump_predictions.c_str(), "wb");
      if (!dump_)
        throw std::runtime_error("cannot open " + a.dump_predictions);
    }
  }
  ~Replay()
  {
    if (dump_) {
      if (dump_bits_)
        std::fputc(dump_byte_, dump_);
      std::fclose(dump_);
    }
  }

  // A non-branch instruction: BTB lookup always; predictor call only when it can change state.
  void non_branch(uint64_t ip, bool call_predictor)
  {
    auto [target, always_taken] = btb_.prediction(ip);
    if (call_predictor)
      record_prediction(pred_.predict(ip, target, always_taken, NOT_BRANCH));
  }

  void branch(uint64_t ip, uint8_t type, bool taken, uint64_t target, uint64_t instr_index, PerBranch* pb)
  {
    auto [predicted_target, always_taken] = btb_.prediction(ip);
    const bool raw = pred_.predict(ip, predicted_target, always_taken, type);
    record_prediction(raw);
    const bool final_pred = raw || always_taken;
    if (!final_pred)
      predicted_target = 0;
    const bool cond_like = type == BRANCH_CONDITIONAL || type == BRANCH_OTHER;
    const bool mispredict = predicted_target != target || (cond_like && taken != final_pred);
    if (instr_index >= args_.warmup && instr_index < args_.warmup + args_.sim) {
      auto& tc = types_[type];
      ++tc.count;
      tc.taken += taken;
      tc.mispredicts += mispredict;
      if (type == BRANCH_CONDITIONAL) {
        dir_mispredicts_ += raw != taken;
        dir_mispredicts_final_ += final_pred != taken;
        if (pb) {
          pb->ip = ip;
          ++pb->execs;
          pb->taken += taken;
          pb->dir_mispredicts += raw != taken;
        }
      }
    }
    btb_.update(ip, target, taken, type);
    pred_.update(ip, target, taken, type);
  }

  void write_json(std::ostream& o, uint64_t roi_instructions, double elapsed) const
  {
    uint64_t total_branches = 0, total_mispredicts = 0;
    o << "{\n  \"tool\": \"bpsim\",\n  \"predictor\": \"" << args_.predictor << "\",\n  \"config\": \"" << args_.config << "\",\n  \"mode\": \""
      << (args_.stream.empty() ? "raw" : "stream") << "\",\n  \"input\": \"" << (args_.stream.empty() ? args_.trace : args_.stream) << "\",\n  \"warmup\": " << args_.warmup
      << ",\n  \"sim\": " << args_.sim << ",\n  \"roi_instructions\": " << roi_instructions << ",\n  \"branch_types\": {";
    for (std::size_t t = 0; t + 1 < types_.size(); ++t) {
      o << (t ? ", " : "") << "\"" << bst::TYPE_NAMES[t] << "\": {\"count\": " << types_[t].count << ", \"taken\": " << types_[t].taken
        << ", \"mispredicts\": " << types_[t].mispredicts << "}";
      total_branches += types_[t].count;
      total_mispredicts += types_[t].mispredicts;
    }
    const auto& c = types_[BRANCH_CONDITIONAL];
    const double k = static_cast<double>(roi_instructions) / 1000.0;
    o << "},\n  \"conditional\": {\"count\": " << c.count << ", \"taken\": " << c.taken << ", \"dir_mispredicts\": " << dir_mispredicts_
      << ", \"dir_mispredicts_final\": " << dir_mispredicts_final_ << ", \"mispredicts_champsim\": " << c.mispredicts << "},\n  \"mpki\": {\"conditional_dir\": "
      << static_cast<double>(dir_mispredicts_) / k << ", \"conditional_champsim\": " << static_cast<double>(c.mispredicts) / k << ", \"all_champsim\": " << static_cast<double>(total_mispredicts) / k
      << "},\n  \"total_branches\": " << total_branches << ",\n  \"predict_calls\": " << predict_calls_ << ",\n  \"prediction_digest\": \"" << std::hex << digest_
      << std::dec << "\",\n  \"predictor_report\": " << pred_.report().details_json << ",\n  \"elapsed_s\": " << elapsed << "\n}\n";
  }

private:
  void record_prediction(bool p)
  {
    ++predict_calls_;
    digest_ = (digest_ ^ (p ? 0x9E3779B97F4A7C15ULL : 0x7F4A7C159E3779B9ULL)) * 0x100000001B3ULL;
    if (dump_) {
      dump_byte_ = static_cast<uint8_t>(dump_byte_ | (p ? 1u : 0u) << dump_bits_);
      if (++dump_bits_ == 8) {
        std::fputc(dump_byte_, dump_);
        dump_byte_ = 0;
        dump_bits_ = 0;
      }
    }
  }

  IPredictor& pred_;
  IBtb& btb_;
  const Args& args_;
  std::array<TypeCount, 8> types_{};
  uint64_t dir_mispredicts_ = 0, dir_mispredicts_final_ = 0, predict_calls_ = 0;
  uint64_t digest_ = 0xcbf29ce484222325ULL;
  FILE* dump_ = nullptr;
  uint8_t dump_byte_ = 0;
  unsigned dump_bits_ = 0;
};

struct MappedStream {
  const bst::Header* header = nullptr;
  const bst::StaticEntry* statics = nullptr;
  const uint64_t* records = nullptr;
  const uint64_t* overrides = nullptr;
  void* base = nullptr;
  std::size_t size = 0;

  explicit MappedStream(const std::string& path)
  {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0)
      throw std::runtime_error("cannot open " + path);
    struct stat st {};
    ::fstat(fd, &st);
    size = static_cast<std::size_t>(st.st_size);
    base = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (base == MAP_FAILED)
      throw std::runtime_error("mmap failed for " + path);
    ::madvise(base, size, MADV_SEQUENTIAL);
    auto* p = static_cast<const char*>(base);
    header = reinterpret_cast<const bst::Header*>(p);
    if (std::memcmp(header->magic, bst::MAGIC, sizeof header->magic) != 0 || header->version != bst::VERSION)
      throw std::runtime_error("not a branch stream (or wrong version): " + path);
    statics = reinterpret_cast<const bst::StaticEntry*>(p + header->header_bytes);
    records = reinterpret_cast<const uint64_t*>(statics + header->n_static);
    overrides = records + header->n_records;
    if (reinterpret_cast<const char*>(overrides + header->n_override) != p + size)
      throw std::runtime_error("truncated branch stream: " + path);
  }
  ~MappedStream() { ::munmap(base, size); }
};

void write_per_branch(const std::string& path, const std::vector<PerBranch>& v)
{
  std::ofstream o(path);
  o << "ip,execs,taken,dir_mispredicts\n";
  for (const auto& b : v)
    if (b.execs)
      o << std::hex << "0x" << b.ip << std::dec << "," << b.execs << "," << b.taken << "," << b.dir_mispredicts << "\n";
}

int run(const Args& a)
{
  auto pred = make_predictor(a.predictor, a.config);
  auto btb = make_basic_btb();
  Replay replay(*pred, *btb, a);
  const uint64_t end = a.warmup + a.sim;
  std::vector<PerBranch> per_branch;
  uint64_t roi = 0;
  const auto t0 = std::chrono::steady_clock::now();

  if (!a.stream.empty()) {
    if (pred->predict_has_side_effects())
      throw std::runtime_error("this predictor changes state on every predict call; replay it with --trace (raw mode)");
    MappedStream s(a.stream);
    if (s.header->n_instr_decoded < end)
      throw std::runtime_error("stream holds fewer instructions than warmup + sim");
    per_branch.resize(s.header->n_static);
    uint64_t index_plus_one = 0, next_override = 0;
    for (uint64_t r = 0; r < s.header->n_records; ++r) {
      const uint64_t rec = s.records[r];
      index_plus_one += rec >> bst::DELTA_SHIFT;
      const uint64_t index = index_plus_one - 1;
      if (index >= end)
        break;
      const auto id = rec & bst::ID_MASK;
      const auto& st = s.statics[id];
      if (rec & bst::TOUCH_BIT) {
        replay.non_branch(st.ip, false);
        continue;
      }
      const bool taken = rec & bst::TAKEN_BIT;
      uint64_t target = 0;
      if (taken)
        target = (rec & bst::OVERRIDE_BIT) ? s.overrides[next_override++] : st.taken_target;
      replay.branch(st.ip, st.type, taken, target, index, a.per_branch_out.empty() ? nullptr : &per_branch[id]);
    }
    roi = a.sim;
  } else {
    auto reader = get_tracereader(a.trace, 0, false, true);
    const bool every = pred->predict_has_side_effects();
    std::unordered_map<uint64_t, std::size_t> slot;
    for (uint64_t i = 0; i < end; ++i) {
      auto instr = reader();
      const uint64_t ip = instr.ip.to<uint64_t>();
      if (!instr.is_branch) {
        replay.non_branch(ip, every);
        continue;
      }
      PerBranch* pb = nullptr;
      if (!a.per_branch_out.empty()) {
        auto [it, inserted] = slot.try_emplace(ip * 8 + instr.branch, per_branch.size());
        if (inserted)
          per_branch.emplace_back();
        pb = &per_branch[it->second];
      }
      replay.branch(ip, static_cast<uint8_t>(instr.branch), instr.branch_taken, instr.branch_target.to<uint64_t>(), i, pb);
    }
    roi = a.sim;
  }

  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  if (!a.per_branch_out.empty())
    write_per_branch(a.per_branch_out, per_branch);
  if (a.json_out.empty()) {
    replay.write_json(std::cout, roi, elapsed);
  } else {
    std::string tmp = a.json_out + ".tmp";
    {
      std::ofstream o(tmp);
      replay.write_json(o, roi, elapsed);
    }
    if (std::rename(tmp.c_str(), a.json_out.c_str()) != 0)
      throw std::runtime_error("cannot write " + a.json_out);
  }
  return 0;
}
}  // namespace

int main(int argc, char** argv)
{
  try {
    return run(parse(argc, argv));
  } catch (const std::exception& e) {
    std::cerr << "bpsim: " << e.what() << "\n";
    return 1;
  }
}
