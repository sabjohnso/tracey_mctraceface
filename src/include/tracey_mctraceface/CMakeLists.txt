configure_file(config.hpp.in config.hpp)

xb_add_library(
  TARGET fxt_types
  ENCODING ${CMAKE_CURRENT_SOURCE_DIR}/fxt.bes.xml
  MODE HEADER_ONLY
  BINARY_ONLY)
