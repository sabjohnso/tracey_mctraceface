tracey_mctraceface_add_test(cli_acceptance)

# Acceptance tests need the main binary and matmul built first
add_dependencies(cli_acceptance_test tracey_mctraceface)
