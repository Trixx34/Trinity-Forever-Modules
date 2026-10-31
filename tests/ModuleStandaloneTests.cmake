# Registers the standalone Module/Bot test programs from src/server/game/Modules/tests in CTest.
# Included from tests/CMakeLists.txt, so it is only active when BUILD_TESTING is on. Every program has its own main() and reports through its exit code.
# Each one is its own small executable, built from only the core sources it exercises; none of them links the game library or needs a database,
# a world or a TCP port (sockets are Unix sockets named after the pid, shared memory and temp dirs likewise).
#
#   needs nothing:       BotTalentSpecTest, ModuleBotOwnershipTest, ModuleConfigTest, ModuleStoreRulesTest, ModuleApprovalTest (OpenSSL), ModuleTier2Test (Boost.Interprocess)
#   needs the SDK tree:  ModuleWireInterop, ModuleSessionTest (the SDK's .proto files, compiled here with the system protobuf)
#   deferred:            ModuleHostTest (needs the SDK's minimal_module and a built EventModule plus child processes; see the comment at the bottom)
#
# FOREVER_SDK_DIR (cache variable): the forever-module-sdk checkout. Unset or without proto/envelope.proto: the two SDK tests are not registered.
# CTest label: modules. Fixture "module_tests_tmp": a cleanup test removes what an aborted run could leave behind (shared memory, sockets, temp dirs).

set(FOREVER_SDK_DIR "" CACHE PATH "forever-module-sdk checkout; enables the SDK-dependent standalone module tests (ModuleWireInterop, ModuleSessionTest)")

if(NOT UNIX)
  message(STATUS "Module standalone tests: skipped (POSIX only)")
  return()
endif()

set(MODULE_TEST_SRC "${CMAKE_SOURCE_DIR}/src/server/game/Modules")
set(MODULE_TEST_BOTS "${CMAKE_SOURCE_DIR}/src/server/game/Bots")
set(MODULE_TEST_DIR "${MODULE_TEST_SRC}/tests")

# Include paths and libraries shared by all of them. The stub Log.h/Config.h in tests/stubs stand in for the world server's.
add_library(module-test-base INTERFACE)
target_include_directories(module-test-base
  INTERFACE
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/common/Cryptography
    ${CMAKE_SOURCE_DIR}/src/common/Debugging
    ${MODULE_TEST_DIR}/stubs
    ${MODULE_TEST_SRC}
    ${MODULE_TEST_BOTS})
target_link_libraries(module-test-base
  INTERFACE
    trinity-core-interface
    boost
    fmt
    openssl
    threads)

set(MODULE_TEST_TARGETS "")

# module_test(<name> <test source> [extra sources...])
function(module_test name testSource)
  add_executable(${name} ${MODULE_TEST_DIR}/${testSource})
  foreach(extra IN LISTS ARGN)
    target_sources(${name} PRIVATE ${extra})
  endforeach()
  target_link_libraries(${name} PRIVATE module-test-base)
  set_target_properties(${name} PROPERTIES FOLDER "tests")
  add_test(NAME modules.${name} COMMAND ${name})
  set_tests_properties(modules.${name} PROPERTIES
    TIMEOUT 120
    LABELS modules
    FIXTURES_REQUIRED module_tests_tmp)
  set(MODULE_TEST_TARGETS ${MODULE_TEST_TARGETS} ${name} PARENT_SCOPE)
endfunction()

module_test(module_bot_talent_spec_test BotTalentSpecTest.cpp)
module_test(module_bot_ownership_test ModuleBotOwnershipTest.cpp ${MODULE_TEST_SRC}/ModuleBotOwnership.cpp)
module_test(module_config_test ModuleConfigTest.cpp ${MODULE_TEST_SRC}/ModuleConfig.cpp)
module_test(module_store_rules_test ModuleStoreRulesTest.cpp ${MODULE_TEST_SRC}/ModuleStoreRules.cpp ${MODULE_TEST_SRC}/ModuleWire.cpp)
module_test(module_approval_test ModuleApprovalTest.cpp ${MODULE_TEST_SRC}/ModuleApproval.cpp ${MODULE_TEST_SRC}/ModuleManifestInfo.cpp
  ${MODULE_TEST_DIR}/stubs/ErrorsStub.cpp)
module_test(module_tier2_test ModuleTier2Test.cpp ${MODULE_TEST_SRC}/ModuleTier2.cpp)

# SDK-dependent tests: the SDK's proto files are compiled with the system protobuf (the SDK itself does the same). The core's bundled protobuf
# (2.6.1) is not used here, and protoc is not part of it.
set(MODULE_SDK_TESTS OFF)
if(NOT FOREVER_SDK_DIR)
  message(STATUS "Module standalone tests: FOREVER_SDK_DIR is not set, ModuleWireInterop and ModuleSessionTest are not registered")
elseif(NOT EXISTS "${FOREVER_SDK_DIR}/proto/envelope.proto")
  message(STATUS "Module standalone tests: ${FOREVER_SDK_DIR}/proto/envelope.proto not found, ModuleWireInterop and ModuleSessionTest are not registered")
else()
  find_package(Protobuf QUIET)
  if(NOT Protobuf_FOUND OR NOT TARGET protobuf::libprotobuf OR NOT Protobuf_PROTOC_EXECUTABLE)
    message(STATUS "Module standalone tests: system protobuf/protoc not found, ModuleWireInterop and ModuleSessionTest are not registered")
  else()
    set(MODULE_SDK_TESTS ON)
  endif()
endif()

if(MODULE_SDK_TESTS)
  set(MODULE_PROTO_OUT "${CMAKE_CURRENT_BINARY_DIR}/sdk_proto")
  file(MAKE_DIRECTORY "${MODULE_PROTO_OUT}")
  set(MODULE_PROTO_SOURCES "")
  foreach(proto handshake events control actions envelope)
    add_custom_command(
      OUTPUT "${MODULE_PROTO_OUT}/${proto}.pb.cc" "${MODULE_PROTO_OUT}/${proto}.pb.h"
      COMMAND ${Protobuf_PROTOC_EXECUTABLE} --cpp_out "${MODULE_PROTO_OUT}" -I "${FOREVER_SDK_DIR}/proto" "${FOREVER_SDK_DIR}/proto/${proto}.proto"
      DEPENDS "${FOREVER_SDK_DIR}/proto/${proto}.proto"
      COMMENT "Generating protobuf sources for SDK ${proto}.proto"
      VERBATIM)
    list(APPEND MODULE_PROTO_SOURCES "${MODULE_PROTO_OUT}/${proto}.pb.cc")
  endforeach()

  add_library(module-test-sdk-proto STATIC ${MODULE_PROTO_SOURCES})
  target_include_directories(module-test-sdk-proto SYSTEM PUBLIC "${MODULE_PROTO_OUT}")
  target_link_libraries(module-test-sdk-proto
    PUBLIC
      protobuf::libprotobuf
    PRIVATE
      trinity-default-interface
      trinity-no-warning-interface)
  set_target_properties(module-test-sdk-proto PROPERTIES FOLDER "tests")

  module_test(module_wire_interop ModuleWireInterop.cpp ${MODULE_TEST_SRC}/ModuleWire.cpp)
  target_link_libraries(module_wire_interop PRIVATE module-test-sdk-proto)
  module_test(module_session_test ModuleSessionTest.cpp ${MODULE_TEST_SRC}/ModuleSession.cpp ${MODULE_TEST_SRC}/ModuleSocket.cpp
    ${MODULE_TEST_SRC}/ModuleWire.cpp)
  target_link_libraries(module_session_test PRIVATE module-test-sdk-proto)
  message(STATUS "Module standalone tests: SDK tests registered (FOREVER_SDK_DIR=${FOREVER_SDK_DIR})")
endif()

# Removes what an aborted test could leave behind; runs after all of them (and also when one failed).
add_test(NAME modules.cleanup COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_LIST_DIR}/ModuleStandaloneCleanup.cmake)
set_tests_properties(modules.cleanup PROPERTIES TIMEOUT 30 LABELS modules FIXTURES_CLEANUP module_tests_tmp)

# So that building the existing "tests" target (what the Jenkins analysis job builds) builds these as well.
add_dependencies(tests ${MODULE_TEST_TARGETS})

list(LENGTH MODULE_TEST_TARGETS MODULE_TEST_COUNT)
message(STATUS "Module standalone tests: ${MODULE_TEST_COUNT} registered (label: modules)")

# Deferred: ModuleHostTest runs ModuleMgr against real module processes (the SDK's minimal_module, /bin/sleep and tests/EventModule.cpp built against the
# SDK library) and takes their paths as arguments. It needs a built SDK tree (library and example) and a core ModuleMgr compiled with the stub Config.h;
# register it once the job can hand over the SDK build directory.
