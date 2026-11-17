// Uniform interface over ChampSim branch-predictor modules, used by the bpsim harness.
// Each adapter lives in its own translation unit (adapters/*.cc) because several modules
// share helper class names and include-guard conventions.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

struct PredictorReport {
  uint64_t storage_bits = 0;  // 0 when the module does not report it
  std::string details_json = "{}";
};

class IPredictor
{
public:
  virtual ~IPredictor() = default;
  // Mirrors O3_CPU::impl_predict_branch: called for every instruction in raw mode, for branches in stream mode.
  virtual bool predict(uint64_t ip, uint64_t predicted_target, bool always_taken, uint8_t branch_type) = 0;
  // Mirrors O3_CPU::impl_last_branch_result: called for branches only, right after predict.
  virtual void update(uint64_t ip, uint64_t target, bool taken, uint8_t branch_type) = 0;
  // True if predict() changes predictor state (then every instruction must be replayed: raw mode).
  virtual bool predict_has_side_effects() const = 0;
  virtual PredictorReport report() const { return {}; }
};

std::unique_ptr<IPredictor> make_predictor(const std::string& name, const std::string& config_path);

// The BTB used by ChampSim's default configuration (btb/basic_btb).
class IBtb
{
public:
  virtual ~IBtb() = default;
  virtual std::pair<uint64_t, bool> prediction(uint64_t ip) = 0;
  virtual void update(uint64_t ip, uint64_t target, bool taken, uint8_t branch_type) = 0;
};
std::unique_ptr<IBtb> make_basic_btb();
