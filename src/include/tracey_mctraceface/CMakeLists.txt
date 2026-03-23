configure_file(config.hpp.in config.hpp)

xb_add_library(
  TARGET fxt_types
  ENCODING ${CMAKE_CURRENT_SOURCE_DIR}/fxt.bes.xml
  OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/xb_gen/fxt
  MODE HEADER_ONLY
  BINARY_ONLY)

xb_add_library(
  TARGET perf_event_types
  ENCODING ${CMAKE_CURRENT_SOURCE_DIR}/perf_event.bes.xml
  OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/xb_gen/perf_event
  MODE HEADER_ONLY
  BINARY_ONLY)
