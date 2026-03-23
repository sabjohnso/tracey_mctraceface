add_library(tracey_mctraceface_fxt_writer fxt_writer.cpp)

target_link_libraries(tracey_mctraceface_fxt_writer
  PUBLIC tracey_mctraceface::header fxt_types)

set_target_properties(tracey_mctraceface_fxt_writer PROPERTIES
  EXPORT_NAME fxt_writer
  CXX_STANDARD ${tracey_mctraceface_CXX_STANDARD})

add_library(tracey_mctraceface::fxt_writer ALIAS tracey_mctraceface_fxt_writer)
