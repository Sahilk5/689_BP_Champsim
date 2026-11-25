// Adapter for Seznec's CBP-2016 TAGE-SC-L (fetched by fetch_tage.sh). One translation unit per size:
// predictor.h defines globals and macros, so it is included inside its own namespace.
//  * As in the contest framework, only conditional branches are predicted; others go to TrackOtherInst.
//  * The predictor uses a conditional branch's target even when it is not taken (IMLI: backward loops).
//    ChampSim reports target 0 then, so the adapter supplies the branch's last taken target (decode knows
//    it); a branch never taken so far is treated as forward.
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>
#include <vector>
#include "../predictor_iface.h"
#include "../tage_shim/utils.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wconversion"
namespace TAGE_NS
{
#include TAGE_HEADER
}
#pragma GCC diagnostic pop

namespace
{
class Adapter final : public IPredictor
{
  TAGE_NS::PREDICTOR p;
  std::unordered_map<uint64_t, uint64_t> taken_target;
  bool last = false;

  static OpType op(uint8_t t)
  {
    switch (t) {  // champsim branch_type
    case 0: return OPTYPE_JMP_DIRECT_UNCOND;
    case 1: return OPTYPE_JMP_INDIRECT_UNCOND;
    case 2: return OPTYPE_JMP_DIRECT_COND;
    case 3: return OPTYPE_CALL_DIRECT_UNCOND;
    case 4: return OPTYPE_CALL_INDIRECT_UNCOND;
    case 5: return OPTYPE_RET_UNCOND;
    default: return OPTYPE_JMP_DIRECT_UNCOND;
    }
  }

public:
  bool predict(uint64_t ip, uint64_t, bool, uint8_t type) override
  {
    if (type != 2)
      return true;
    return last = p.GetPrediction(ip);
  }
  void update(uint64_t ip, uint64_t target, bool taken, uint8_t type) override
  {
    if (type != 2) {
      p.TrackOtherInst(ip, op(type), taken, target);
      return;
    }
    auto it = taken_target.find(ip);
    if (taken)
      taken_target[ip] = target;
    else
      target = it == taken_target.end() ? ip + 4 : it->second;
    p.UpdatePredictor(ip, OPTYPE_JMP_DIRECT_COND, taken, last, target);
  }
  bool predict_has_side_effects() const override { return false; }  // only called for conditional branches
  PredictorReport report() const override { return {uint64_t{TAGE_KB} * 8192, "{\"nominal_kib\": " + std::to_string(TAGE_KB) + "}"}; }
};
}  // namespace
std::unique_ptr<IPredictor> TAGE_MAKE() { return std::make_unique<Adapter>(); }
