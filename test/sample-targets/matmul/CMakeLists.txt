json_commander_add_executable(matmul
  SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/matmul.json
  MAIN matmul::run
  FROM_HEADER matmul_main.hpp
  matmul_main.cpp)

set_target_properties(matmul PROPERTIES
  CXX_STANDARD ${tracey_mctraceface_CXX_STANDARD})
