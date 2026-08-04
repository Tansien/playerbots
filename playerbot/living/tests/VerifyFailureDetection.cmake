# Runs the harness in --verify-failure-detection mode and requires the exact
# failure contract, not merely a nonzero exit (a usage error exits 2 and must
# not satisfy this test): exit code 1, the injected test reported as FAIL, and
# the failure counted in the summary.
execute_process(COMMAND "${RUNNER}" --verify-failure-detection
  OUTPUT_VARIABLE output ERROR_VARIABLE output RESULT_VARIABLE result)

if(NOT result EQUAL 1)
  message(FATAL_ERROR "expected exit code 1, got '${result}'; output:\n${output}")
endif()

if(NOT output MATCHES "FAIL VerifyFailureDetection_DeliberateFailure")
  message(FATAL_ERROR "the injected failing test was not reported; output:\n${output}")
endif()

if(NOT output MATCHES ", 1 failed")
  message(FATAL_ERROR "the injected failure was not counted in the summary; output:\n${output}")
endif()
