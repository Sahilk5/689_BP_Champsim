#include "wnn.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "instruction.h"

static_assert(BRANCH_CONDITIONAL == 2, "wnn_core assumes BRANCH_CONDITIONAL == 2");

wnn::wnn(O3_CPU* cpu) : champsim::modules::branch_predictor(cpu)
{
  const char* path = std::getenv("WNN_CONFIG");
  if (path == nullptr) {
    std::cerr << "wnn: set WNN_CONFIG to a predictor config (e.g. branch/wnn/presets/original.json)\n";
    std::exit(2);
  }
  try {
    core = std::make_unique<wnn_core::Predictor>(wnn_core::Config::from_json_file(path));
  } catch (const std::exception& e) {
    std::cerr << "wnn: bad config " << path << ": " << e.what() << "\n";
    std::exit(2);
  }
  auto s = core->storage();
  std::cout << "wnn: config " << path << " (" << core->config().name << "), storage " << s.total_bits() << " bits\n";
}

bool wnn::predict_branch(champsim::address ip) { return core->predict(ip.to<uint64_t>()); }

void wnn::last_branch_result(champsim::address ip, champsim::address branch_target, bool taken, uint8_t branch_type)
{
  core->update(ip.to<uint64_t>(), branch_target.to<uint64_t>(), taken, branch_type);
}
