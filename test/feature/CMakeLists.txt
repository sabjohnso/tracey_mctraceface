tracey_mctraceface_add_test(cli_acceptance)

# Pass the actual binary path so the test works with any build dir / config
target_compile_definitions(cli_acceptance_test PRIVATE
  TRACEY_BINARY_PATH="$<TARGET_FILE:tracey_mctraceface>")

add_dependencies(cli_acceptance_test tracey_mctraceface)
