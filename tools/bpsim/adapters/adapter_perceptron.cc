// Adapter for the ChampSim module `perceptron` (branch/perceptron/perceptron.h).
#include "branch/perceptron/perceptron.h"
#include "../predictor_iface.h"

namespace
{
class Adapter final : public IPredictor
{
  perceptron p{nullptr};

public:
  bool predict(uint64_t ip, uint64_t, bool, uint8_t) override { return p.predict_branch(champsim::address{ip}); }
  void update(uint64_t ip, uint64_t target, bool taken, uint8_t type) override
  {
    p.last_branch_result(champsim::address{ip}, champsim::address{target}, taken, type);
  }
  bool predict_has_side_effects() const override { return true; }  // shifts spec_global_history in predict_branch
};
} // namespace

std::unique_ptr<IPredictor> make_perceptron() { return std::make_unique<Adapter>(); }
