cmake_minimum_required(VERSION 3.12)

project(tracey_mctraceface VERSION 0.1.0 LANGUAGES C CXX)

option(tracey_mctraceface_BUILD_TESTING "Build the tracey_mctraceface tests" ON)
set(tracey_mctraceface_CXX_STANDARD 20 CACHE STRING "C++ standard for tracey_mctraceface")

include(GNUInstallDirs)
set(tracey_mctraceface_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
set(tracey_mctraceface_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
set(tracey_mctraceface_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
set(tracey_mctraceface_INSTALL_CONFDIR ${tracey_mctraceface_INSTALL_LIBDIR}/cmake/tracey_mctraceface-${PROJECT_VERSION})

list(INSERT CMAKE_MODULE_PATH 0 ${PROJECT_SOURCE_DIR}/cmake)
include(tracey_mctraceface_deps)

add_subdirectory(src)

if(tracey_mctraceface_BUILD_TESTING)
  include(CTest)
  if(BUILD_TESTING)
    add_subdirectory(test)
  endif()
endif()

install(EXPORT tracey_mctraceface_EXPORTS
  NAMESPACE tracey_mctraceface::
  FILE tracey_mctraceface-exports.cmake
  DESTINATION ${tracey_mctraceface_INSTALL_CONFDIR})

configure_file(tracey_mctraceface-config.cmake.in tracey_mctraceface-config.cmake)
install(FILES ${PROJECT_BINARY_DIR}/tracey_mctraceface-config.cmake
  DESTINATION ${tracey_mctraceface_INSTALL_CONFDIR})
