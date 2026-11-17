// Adapter for ChampSim's basic_btb, so harness misprediction counts match ChampSim's
// (ChampSim counts a taken branch whose BTB target is wrong as a mispredict).
#include "btb/basic_btb/basic_btb.h"
#include "../predictor_iface.h"

namespace
{
class Btb final : public IBtb
{
  basic_btb b{};

public:
  std::pair<uint64_t, bool> prediction(uint64_t ip) override
  {
    auto [target, always_taken] = b.btb_prediction(champsim::address{ip});
    return {target.to<uint64_t>(), always_taken};
  }
  void update(uint64_t ip, uint64_t target, bool taken, uint8_t type) override
  {
    b.update_btb(champsim::address{ip}, champsim::address{target}, taken, type);
  }
};
}  // namespace

std::unique_ptr<IBtb> make_basic_btb() { return std::make_unique<Btb>(); }
