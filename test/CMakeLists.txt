find_package(Catch2 REQUIRED)

macro(tracey_mctraceface_add_test test_name)
  set(exe_name ${test_name}_test)
  set(src_name ${exe_name}.cpp)
  add_executable(${exe_name} ${src_name})
  target_link_libraries(${exe_name} PRIVATE
    tracey_mctraceface::header
    fxt_types
    Catch2::Catch2WithMain)
  set_target_properties(${exe_name}
    PROPERTIES
    CXX_STANDARD ${tracey_mctraceface_CXX_STANDARD})
  add_test(${test_name} ${exe_name})
endmacro()

macro(tracey_mctraceface_add_fxt_test test_name)
  set(exe_name ${test_name}_test)
  set(src_name ${exe_name}.cpp)
  add_executable(${exe_name} ${src_name})
  target_link_libraries(${exe_name} PRIVATE
    tracey_mctraceface::fxt_writer
    Catch2::Catch2WithMain)
  set_target_properties(${exe_name}
    PROPERTIES
    CXX_STANDARD ${tracey_mctraceface_CXX_STANDARD})
  add_test(${test_name} ${exe_name})
endmacro()

add_subdirectory(unit)
add_subdirectory(feature)
