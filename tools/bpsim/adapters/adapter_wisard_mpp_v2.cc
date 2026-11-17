// Adapter for the ChampSim module `wisard_mpp_v2` (tools/bpsim/reference/wisard_mpp_v2/wisard.h).
#include "tools/bpsim/reference/wisard_mpp_v2/wisard.h"
#include "../predictor_iface.h"

namespace
{
class Adapter final : public IPredictor
{
  wisard_mpp_v2 p{nullptr};

public:
  bool predict(uint64_t ip, uint64_t, bool, uint8_t) override { return p.predict_branch(champsim::address{ip}); }
  void update(uint64_t ip, uint64_t target, bool taken, uint8_t type) override
  {
    p.last_branch_result(champsim::address{ip}, champsim::address{target}, taken, type);
  }
  bool predict_has_side_effects() const override { return true; }
};
} // namespace

std::unique_ptr<IPredictor> make_wisard_mpp_v2() { return std::make_unique<Adapter>(); }
