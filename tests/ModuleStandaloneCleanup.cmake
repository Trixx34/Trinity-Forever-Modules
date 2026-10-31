# Run by the modules.cleanup CTest test: remove leftovers of the standalone module tests (all named after the test process id).
if(DEFINED ENV{TMPDIR} AND NOT "$ENV{TMPDIR}" STREQUAL "")
  set(tmp "$ENV{TMPDIR}")
else()
  set(tmp "/tmp")
endif()
file(GLOB leftovers
  "/dev/shm/trinity_module_test_*"
  "${tmp}/fm-session-*.sock"
  "/tmp/fm-session-*.sock"
  "${tmp}/module-approval-test-*"
  "${tmp}/module-host-test-*")
list(REMOVE_DUPLICATES leftovers)
foreach(f IN LISTS leftovers)
  message(STATUS "removing leftover ${f}")
  file(REMOVE_RECURSE "${f}")
endforeach()
