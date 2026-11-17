// bst_compact: convert a full branch stream (v1) into the compact form (v2).
//
// v1 holds every BTB lookup ChampSim makes that can change BTB state: one per branch plus
// "touch" records for non-branch instructions in a branch's 4-byte group. The BTB's outputs do
// not depend on the direction predictor, so this tool replays basic_btb once over the v1 stream,
// in ChampSim's order (lookup; then, for branches, update), and stores per branch record:
//   ALWAYS_TAKEN_BIT  the BTB returned always_taken
//   BTB_RIGHT_BIT     the BTB's target equals the actual target (taken) or is 0 (not taken)
// Touch records are dropped. bpsim replays v2 without simulating the BTB.
//
// Usage: bst_compact --in F.bst --out F.bstc
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "predictor_iface.h"
#include "stream_format.h"

int main(int argc, char** argv)
{
  try {
    std::string in, out;
    for (int i = 1; i + 1 < argc; i += 2) {
      std::string k = argv[i];
      if (k == "--in")
        in = argv[i + 1];
      else if (k == "--out")
        out = argv[i + 1];
      else
        throw std::runtime_error("unknown argument " + k);
    }
    if (in.empty() || out.empty())
      throw std::runtime_error("usage: bst_compact --in F.bst --out F.bstc");

    int fd = ::open(in.c_str(), O_RDONLY);
    if (fd < 0)
      throw std::runtime_error("cannot open " + in);
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
    if (std::memcmp(h.magic, bst::MAGIC, sizeof h.magic) != 0 || h.version != bst::VERSION)
      throw std::runtime_error("input is not a v1 branch stream");
    const auto* statics = reinterpret_cast<const bst::StaticEntry*>(p + h.header_bytes);
    const auto* records = reinterpret_cast<const uint64_t*>(statics + h.n_static);
    const auto* overrides = records + h.n_records;

    auto btb = make_basic_btb();
    std::vector<uint64_t> outrec;
    outrec.reserve(h.n_records);
    uint64_t pending_delta = 0, next_override = 0;
    for (uint64_t r = 0; r < h.n_records; ++r) {
      const uint64_t rec = records[r];
      pending_delta += rec >> bst::DELTA_SHIFT;
      const auto& s = statics[rec & bst::ID_MASK];
      if (rec & bst::TOUCH_BIT) {
        btb->prediction(s.ip);
        continue;
      }
      const bool taken = rec & bst::TAKEN_BIT;
      uint64_t target = 0;
      if (taken)
        target = (rec & bst::OVERRIDE_BIT) ? overrides[next_override++] : s.taken_target;
      auto [btb_target, always_taken] = btb->prediction(s.ip);
      const bool right = taken ? btb_target == target : btb_target == 0;
      btb->update(s.ip, target, taken, s.type);
      if (pending_delta >= (1ULL << (64 - bst::DELTA_SHIFT_V2)))
        throw std::runtime_error("gap between branches exceeds 31 bits");
      outrec.push_back((rec & (bst::ID_MASK | bst::TAKEN_BIT | bst::OVERRIDE_BIT)) | (always_taken ? bst::ALWAYS_TAKEN_BIT : 0) | (right ? bst::BTB_RIGHT_BIT : 0)
                       | (pending_delta << bst::DELTA_SHIFT_V2));
      pending_delta = 0;
    }

    h.version = bst::VERSION_COMPACT;
    h.n_records = outrec.size();
    std::string tmp = out + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f)
      throw std::runtime_error("cannot open " + tmp);
    bool ok = std::fwrite(&h, sizeof h, 1, f) == 1 && std::fwrite(statics, sizeof(bst::StaticEntry), h.n_static, f) == h.n_static
              && std::fwrite(outrec.data(), sizeof(uint64_t), outrec.size(), f) == outrec.size()
              && std::fwrite(overrides, sizeof(uint64_t), h.n_override, f) == h.n_override;
    ok = (std::fclose(f) == 0) && ok;
    ::munmap(base, size);
    if (!ok || std::rename(tmp.c_str(), out.c_str()) != 0)
      throw std::runtime_error("failed writing " + out);
    std::printf("{\"branch_records\": %llu}\n", static_cast<unsigned long long>(outrec.size()));
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "bst_compact: " << e.what() << "\n";
    return 1;
  }
}
