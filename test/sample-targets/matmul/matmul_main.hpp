#pragma once

#include <nlohmann/json.hpp>

namespace matmul {

  auto
  run(const nlohmann::json& config) -> int;

} // namespace matmul
