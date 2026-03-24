json_commander_add_executable(tracey_mctraceface
  SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/tracey_mctraceface.json
  MAIN tracey_mctraceface::run
  FROM_HEADER tracey_mctraceface_main.hpp
  tracey_mctraceface_main.cpp)

target_link_libraries(tracey_mctraceface PRIVATE
  tracey_mctraceface::fxt_writer
  tracey_mctraceface::perf_script_parser
  tracey_mctraceface::stack_reconstructor
  tracey_mctraceface::perf_driver
  tracey_mctraceface::perf_capabilities
  tracey_mctraceface::compressed_sink
  tracey_mctraceface::subprocess
  tracey_mctraceface::trace_filter
  tracey_mctraceface::trace_server
  tracey_mctraceface::background_process
  tracey_mctraceface::perf_capabilities)

set_target_properties(tracey_mctraceface PROPERTIES
  CXX_STANDARD ${tracey_mctraceface_CXX_STANDARD})
