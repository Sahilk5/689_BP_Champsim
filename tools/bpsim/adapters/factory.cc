#include <stdexcept>

#include "../predictor_iface.h"

std::unique_ptr<IPredictor> make_wisard();
std::unique_ptr<IPredictor> make_wisard_mpp_v2();
std::unique_ptr<IPredictor> make_bimodal();
std::unique_ptr<IPredictor> make_gshare();
std::unique_ptr<IPredictor> make_perceptron();
std::unique_ptr<IPredictor> make_hashed_perceptron();
std::unique_ptr<IPredictor> make_wnn(const std::string& config_path);

std::unique_ptr<IPredictor> make_predictor(const std::string& name, const std::string& config_path)
{
  if (name == "wnn") {
    if (config_path.empty())
      throw std::runtime_error("predictor wnn needs --config");
    return make_wnn(config_path);
  }
  if (!config_path.empty())
    throw std::runtime_error("--config only applies to predictor wnn");
  if (name == "wisard")
    return make_wisard();
  if (name == "wisard_mpp_v2")
    return make_wisard_mpp_v2();
  if (name == "bimodal")
    return make_bimodal();
  if (name == "gshare")
    return make_gshare();
  if (name == "perceptron")
    return make_perceptron();
  if (name == "hashed_perceptron")
    return make_hashed_perceptron();
  throw std::runtime_error("unknown predictor " + name);
}
