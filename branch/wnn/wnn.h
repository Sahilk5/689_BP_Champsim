#ifndef BRANCH_WNN_H
#define BRANCH_WNN_H

#include <memory>

#include "modules.h"
#include "wnn_core.h"

// ChampSim module for the configurable WiSARD predictor.
// The configuration is read from the JSON file named by the WNN_CONFIG environment variable
// (see branch/wnn/presets/). The same wnn_core code runs in the bpsim replay harness.
class wnn : public champsim::modules::branch_predictor
{
  std::unique_ptr<wnn_core::Predictor> core;

public:
  explicit wnn(O3_CPU* cpu);
  bool predict_branch(champsim::address ip, champsim::address predicted_target, bool always_taken, uint8_t branch_type);
  void last_branch_result(champsim::address ip, champsim::address branch_target, bool taken, uint8_t branch_type);
};

#endif
