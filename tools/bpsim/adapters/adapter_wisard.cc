// Adapter for the ChampSim module `wisard` (branch/wisard/wisard.h).
#include "branch/wisard/wisard.h"
#include "../predictor_iface.h"

namespace
{
class Adapter final : public IPredictor
{
  wisard p{nullptr};

public:
  bool predict(uint64_t ip, uint64_t, bool, uint8_t) override { return p.predict_branch(champsim::address{ip}); }
  void update(uint64_t ip, uint64_t target, bool taken, uint8_t type) override
  {
    p.last_branch_result(champsim::address{ip}, champsim::address{target}, taken, type);
  }
  bool predict_has_side_effects() const override { return true; }
};
} // namespace

std::unique_ptr<IPredictor> make_wisard() { return std::make_unique<Adapter>(); }
