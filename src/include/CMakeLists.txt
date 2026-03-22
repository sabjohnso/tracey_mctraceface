add_subdirectory(tracey_mctraceface)

add_library(tracey_mctraceface_header INTERFACE)
set_target_properties(tracey_mctraceface_header PROPERTIES EXPORT_NAME header)
target_include_directories(tracey_mctraceface_header INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<INSTALL_INTERFACE:${tracey_mctraceface_INSTALL_INCLUDEDIR}>)
add_library(tracey_mctraceface::header ALIAS tracey_mctraceface_header)

install(TARGETS tracey_mctraceface_header
  EXPORT tracey_mctraceface_EXPORTS
  RUNTIME DESTINATION ${tracey_mctraceface_INSTALL_BINDIR}
  LIBRARY DESTINATION ${tracey_mctraceface_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${tracey_mctraceface_INSTALL_LIBDIR})

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/tracey_mctraceface/config.hpp
  DESTINATION ${tracey_mctraceface_INSTALL_INCLUDEDIR}/tracey_mctraceface)
