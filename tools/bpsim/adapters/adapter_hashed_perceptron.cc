// Adapter for the ChampSim module `hashed_perceptron` (branch/hashed_perceptron/hashed_perceptron.h).
#include "branch/hashed_perceptron/hashed_perceptron.h"
#include "../predictor_iface.h"

namespace
{
class Adapter final : public IPredictor
{
  hashed_perceptron p{nullptr};

public:
  bool predict(uint64_t ip, uint64_t, bool, uint8_t) override { return p.predict_branch(champsim::address{ip}); }
  void update(uint64_t ip, uint64_t target, bool taken, uint8_t type) override
  {
    p.last_branch_result(champsim::address{ip}, champsim::address{target}, taken, type);
  }
  bool predict_has_side_effects() const override { return false; }
  PredictorReport report() const override { return {uint64_t{12} * (std::size_t{1} << HP_TABLE_BITS) * 8, "{}"}; }
};
} // namespace

std::unique_ptr<IPredictor> make_hashed_perceptron() { return std::make_unique<Adapter>(); }
