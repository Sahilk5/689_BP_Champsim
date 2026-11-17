// Adapter for the ChampSim module `gshare` (branch/gshare/gshare.h).
#include "branch/gshare/gshare.h"
#include "../predictor_iface.h"

namespace
{
class Adapter final : public IPredictor
{
  gshare p{nullptr};

public:
  bool predict(uint64_t ip, uint64_t, bool, uint8_t) override { return p.predict_branch(champsim::address{ip}); }
  void update(uint64_t ip, uint64_t target, bool taken, uint8_t type) override
  {
    p.last_branch_result(champsim::address{ip}, champsim::address{target}, taken, type);
  }
  bool predict_has_side_effects() const override { return false; }
};
} // namespace

std::unique_ptr<IPredictor> make_gshare() { return std::make_unique<Adapter>(); }
