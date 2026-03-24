add_library(tracey_mctraceface_fxt_writer fxt_writer.cpp)

target_link_libraries(tracey_mctraceface_fxt_writer
  PUBLIC tracey_mctraceface::header fxt_types)

set_target_properties(tracey_mctraceface_fxt_writer PROPERTIES
  EXPORT_NAME fxt_writer
  CXX_STANDARD ${tracey_mctraceface_CXX_STANDARD})

add_library(tracey_mctraceface::fxt_writer ALIAS tracey_mctraceface_fxt_writer)

add_library(tracey_mctraceface_perf_script_parser perf_script_parser.cpp)

target_link_libraries(tracey_mctraceface_perf_script_parser
  PUBLIC tracey_mctraceface::header)

set_target_properties(tracey_mctraceface_perf_script_parser PROPERTIES
  EXPORT_NAME perf_script_parser
  CXX_STANDARD ${tracey_mctraceface_CXX_STANDARD})

add_library(tracey_mctraceface::perf_script_parser ALIAS tracey_mctraceface_perf_script_parser)

add_library(tracey_mctraceface_shorten_symbol shorten_symbol.cpp)

target_link_libraries(tracey_mctraceface_shorten_symbol
  PUBLIC tracey_mctraceface::header)

set_target_properties(tracey_mctraceface_shorten_symbol PROPERTIES
  EXPORT_NAME shorten_symbol
  CXX_STANDARD ${tracey_mctraceface_CXX_STANDARD})

add_library(tracey_mctraceface::shorten_symbol ALIAS tracey_mctraceface_shorten_symbol)

add_library(tracey_mctraceface_stack_reconstructor stack_reconstructor.cpp)

target_link_libraries(tracey_mctraceface_stack_reconstructor
  PUBLIC tracey_mctraceface::fxt_writer tracey_mctraceface::shorten_symbol)

set_target_properties(tracey_mctraceface_stack_reconstructor PROPERTIES
  EXPORT_NAME stack_reconstructor
  CXX_STANDARD ${tracey_mctraceface_CXX_STANDARD})

add_library(tracey_mctraceface::stack_reconstructor ALIAS tracey_mctraceface_stack_reconstructor)

add_library(tracey_mctraceface_perf_capabilities perf_capabilities.cpp)

target_link_libraries(tracey_mctraceface_perf_capabilities
  PUBLIC tracey_mctraceface::header)

set_target_properties(tracey_mctraceface_perf_capabilities PROPERTIES
  EXPORT_NAME perf_capabilities
  CXX_STANDARD ${tracey_mctraceface_CXX_STANDARD})

add_library(tracey_mctraceface::perf_capabilities ALIAS tracey_mctraceface_perf_capabilities)

add_library(tracey_mctraceface_perf_driver perf_driver.cpp)

target_link_libraries(tracey_mctraceface_perf_driver
  PUBLIC tracey_mctraceface::header tracey_mctraceface::perf_capabilities)

set_target_properties(tracey_mctraceface_perf_driver PROPERTIES
  EXPORT_NAME perf_driver
  CXX_STANDARD ${tracey_mctraceface_CXX_STANDARD})

add_library(tracey_mctraceface::perf_driver ALIAS tracey_mctraceface_perf_driver)

add_library(tracey_mctraceface_file_sink file_sink.cpp)

target_link_libraries(tracey_mctraceface_file_sink
  PUBLIC tracey_mctraceface::header)

set_target_properties(tracey_mctraceface_file_sink PROPERTIES
  EXPORT_NAME file_sink
  CXX_STANDARD ${tracey_mctraceface_CXX_STANDARD})

add_library(tracey_mctraceface::file_sink ALIAS tracey_mctraceface_file_sink)

add_library(tracey_mctraceface_subprocess subprocess.cpp)

target_link_libraries(tracey_mctraceface_subprocess
  PUBLIC tracey_mctraceface::header)

set_target_properties(tracey_mctraceface_subprocess PROPERTIES
  EXPORT_NAME subprocess
  CXX_STANDARD ${tracey_mctraceface_CXX_STANDARD})

add_library(tracey_mctraceface::subprocess ALIAS tracey_mctraceface_subprocess)

add_library(tracey_mctraceface_background_process background_process.cpp)

target_link_libraries(tracey_mctraceface_background_process
  PUBLIC tracey_mctraceface::header)

set_target_properties(tracey_mctraceface_background_process PROPERTIES
  EXPORT_NAME background_process
  CXX_STANDARD ${tracey_mctraceface_CXX_STANDARD})

add_library(tracey_mctraceface::background_process ALIAS tracey_mctraceface_background_process)

add_library(tracey_mctraceface_trace_filter trace_filter.cpp)

target_link_libraries(tracey_mctraceface_trace_filter
  PUBLIC tracey_mctraceface::header)

set_target_properties(tracey_mctraceface_trace_filter PROPERTIES
  EXPORT_NAME trace_filter
  CXX_STANDARD ${tracey_mctraceface_CXX_STANDARD})

add_library(tracey_mctraceface::trace_filter ALIAS tracey_mctraceface_trace_filter)
