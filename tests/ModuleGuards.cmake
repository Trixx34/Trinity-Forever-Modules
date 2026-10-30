# Tier-0 guards for the module system (work package WP-0 of the core test-coverage plan). Included from tests/CMakeLists.txt, so only active with BUILD_TESTING.
#
#   guards.layout_sync          CTest test: the core's ModuleTier2Layout.h and ModuleMgr.h protocol number agree with the SDK's tier2_layout.h and
#                               protocol_version.h (tests/ModuleLayoutSync.cmake). Needs FOREVER_SDK_DIR; reported as SKIPPED without it.
#   module-header-selfcontained Compile-only OBJECT library: every header in src/server/game/Modules compiles when it is the first (and only) thing a
#                               translation unit includes, twice in a row (include guards). It is a dependency of the `tests` target, so a header
#                               that relies on what a .cpp happened to include first (the ModuleBotActions.cpp / GossipDef.h bug class) fails the
#                               test build, not a later full build. The TUs contain two lines each; nothing is linked or run.

set(FOREVER_SDK_DIR "" CACHE PATH "forever-module-sdk checkout; enables the SDK-dependent tests (layout sync, standalone module tests)")

# ---- layout / protocol sync against the SDK ----
add_test(NAME guards.layout_sync
  COMMAND ${CMAKE_COMMAND}
    -DCORE_DIR=${CMAKE_SOURCE_DIR}
    -DSDK_DIR=${FOREVER_SDK_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/ModuleLayoutSync.cmake)
set_tests_properties(guards.layout_sync PROPERTIES
  LABELS guards
  TIMEOUT 60
  SKIP_REGULAR_EXPRESSION "LAYOUT_SYNC_SKIPPED")

# ---- header self-containment ----
set(MODULE_HEADER_DIR "${CMAKE_SOURCE_DIR}/src/server/game/Modules")
file(GLOB MODULE_HEADERS CONFIGURE_DEPENDS RELATIVE "${MODULE_HEADER_DIR}" "${MODULE_HEADER_DIR}/*.h")

# Headers that are deliberately not checked, each with the reason. Everything else in the directory is checked, so a new header is covered by default.
set(MODULE_HEADERS_NOT_STANDALONE "")
# (none at the moment: ModuleDungeonTelegraph.h pulls in Creature.h, Player.h, Map.h ... itself and is checked like the rest)

set(MODULE_HEADER_TUS "")
foreach(header IN LISTS MODULE_HEADERS)
  if(header IN_LIST MODULE_HEADERS_NOT_STANDALONE)
    continue()
  endif()
  get_filename_component(headerName "${header}" NAME_WE)
  set(tu "${CMAKE_CURRENT_BINARY_DIR}/header_selfcontained/${headerName}.cpp")
  file(GENERATE OUTPUT "${tu}"
    CONTENT "// generated: ${header} must compile as the first include of a translation unit, and twice\n#include \"${header}\"\n#include \"${header}\"\n")
  list(APPEND MODULE_HEADER_TUS "${tu}")
endforeach()

if(MODULE_HEADER_TUS)
  add_library(module-header-selfcontained OBJECT ${MODULE_HEADER_TUS})
  # the same include paths and compile options as the game library, but no precompiled header: that would hide a missing include
  target_link_libraries(module-header-selfcontained
    PRIVATE
      trinity-core-interface
      game)
  add_dependencies(module-header-selfcontained game)
  set_target_properties(module-header-selfcontained PROPERTIES FOLDER "tests" DISABLE_PRECOMPILE_HEADERS ON)
  add_dependencies(tests module-header-selfcontained)
endif()
