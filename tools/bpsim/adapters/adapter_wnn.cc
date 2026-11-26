// Adapter for the configurable WiSARD predictor (branch/wnn/wnn_core).
#include <sstream>

#include "../predictor_iface.h"
#include "branch/wnn/wnn_core.h"

namespace
{
class Adapter final : public IPredictor
{
  wnn_core::Predictor p;

public:
  explicit Adapter(const std::string& config_path) : p(wnn_core::Config::from_json_file(config_path)) {}
  bool predict(uint64_t ip, uint64_t, bool, uint8_t type) override { return p.predict(ip, type); }
  void update(uint64_t ip, uint64_t target, bool taken, uint8_t type) override { p.update(ip, target, taken, type); }
  bool predict_has_side_effects() const override { return p.config().history == wnn_core::HistoryPolicy::SpecEveryCall; }
  PredictorReport report() const override
  {
    const auto s = p.storage();
    const auto& st = p.stats();
    std::ostringstream o;
    o << "{\"name\": \"" << p.config().name << "\", \"pool_positions\": " << p.pool_size() << ", \"storage_bits\": {\"discriminators\": " << s.discriminator_bits
      << ", \"local_history\": " << s.lht_bits << ", \"global_history\": " << s.global_history_bits << ", \"path_history\": " << s.path_history_bits
      << ", \"mpp_state\": " << s.mpp_state_bits << ", \"run_length\": " << s.runlen_bits << ", \"side_tables\": " << s.side_table_bits << ", \"total\": " << s.total_bits() << "}, \"counter_reads_per_prediction\": " << 2 * p.config().rams
      << ", \"predict_calls\": " << st.predictions << ", \"ties\": " << st.ties << ", \"unresolved_ties\": " << st.unresolved_ties
      << ", \"bleach_steps_sum\": " << st.bleach_steps_sum << ", \"bleach_steps_max\": " << st.bleach_steps_max << ", \"trainings\": " << st.trainings << ", \"chooser_used_bimodal\": " << st.chooser_used_bimodal << "}";
    return {s.total_bits(), o.str()};
  }
};
}  // namespace

std::unique_ptr<IPredictor> make_wnn(const std::string& config_path) { return std::make_unique<Adapter>(config_path); }
