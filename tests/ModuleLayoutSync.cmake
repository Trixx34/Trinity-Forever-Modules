# CTest script (cmake -P): the core's copy of the Tier 2 layout header must say the same as the SDK's, and the protocol numbers must agree.
#
#   core  src/server/game/Modules/ModuleTier2Layout.h   (kTier2LayoutVersion, ActionType, BotActionStatus, every struct, the offset helpers)
#   sdk   include/forever_module/tier2_layout.h
#   core  src/server/game/Modules/ModuleMgr.h           kModuleProtocolVersion
#   sdk   include/forever_module/protocol_version.h      kProtocolVersion
#
# Comments, blank lines and whitespace are ignored; any other difference fails with the place where the two headers part. Without SDK_DIR
# the test prints LAYOUT_SYNC_SKIPPED, which the test registration maps to a CTest "skipped" result (SKIP_REGULAR_EXPRESSION), so a plain
# core build without an SDK checkout stays green. The same comparison, with a readable diff and struct sizes, is
# forever-ops jenkins/ci/check-layout-sync.py (run in forever-core-analysis without a compile).
#
# usage: cmake -DCORE_DIR=<core source root> -DSDK_DIR=<forever-module-sdk checkout, may be empty> -P ModuleLayoutSync.cmake

if(NOT SDK_DIR)
  message(STATUS "LAYOUT_SYNC_SKIPPED: FOREVER_SDK_DIR is not set, the SDK layout header is not available")
  return()
endif()

set(core_layout "${CORE_DIR}/src/server/game/Modules/ModuleTier2Layout.h")
set(core_mgr "${CORE_DIR}/src/server/game/Modules/ModuleMgr.h")
set(sdk_layout "${SDK_DIR}/include/forever_module/tier2_layout.h")
set(sdk_proto "${SDK_DIR}/include/forever_module/protocol_version.h")
foreach(f IN ITEMS "${core_layout}" "${core_mgr}" "${sdk_layout}" "${sdk_proto}")
  if(NOT EXISTS "${f}")
    message(FATAL_ERROR "layout sync: cannot read ${f} (FOREVER_SDK_DIR=${SDK_DIR} is not a forever-module-sdk checkout?)")
  endif()
endforeach()

# Reads a header and returns the text without comments and with all whitespace collapsed and removed around punctuation.
function(layout_normalize file out)
  file(READ "${file}" text)
  string(REGEX REPLACE "//[^\n]*" "" text "${text}")
  string(REGEX REPLACE "[ \t\r\n]+" " " text "${text}")
  string(REGEX REPLACE " ?([{}();,=:]) ?" "\\1" text "${text}")
  string(STRIP "${text}" text)
  set(${out} "${text}" PARENT_SCOPE)
endfunction()

layout_normalize("${core_layout}" core_text)
layout_normalize("${sdk_layout}" sdk_text)

# the named constants first, so the failure says what changed
foreach(name IN ITEMS kTier2LayoutVersion kMaxTrackedCooldowns kMaxGroundEffects kMaxAuras kMaxQuestLog kMaxQuestObjectives kMaxDamageHits)
  set(core_v "")
  set(sdk_v "")
  if(core_text MATCHES "${name}=([0-9]+)")
    set(core_v "${CMAKE_MATCH_1}")
  endif()
  if(sdk_text MATCHES "${name}=([0-9]+)")
    set(sdk_v "${CMAKE_MATCH_1}")
  endif()
  if(NOT core_v STREQUAL sdk_v)
    message(SEND_ERROR "layout sync: ${name} is '${core_v}' in the core and '${sdk_v}' in the SDK")
  endif()
endforeach()

# the protocol number
set(core_proto "")
set(sdk_proto_v "")
file(READ "${core_mgr}" mgr_text)
if(mgr_text MATCHES "kModuleProtocolVersion = \"([0-9]+)\"")
  set(core_proto "${CMAKE_MATCH_1}")
endif()
file(READ "${sdk_proto}" proto_text)
if(proto_text MATCHES "kProtocolVersion = \"([0-9]+)\"")
  set(sdk_proto_v "${CMAKE_MATCH_1}")
endif()
if(core_proto STREQUAL "" OR sdk_proto_v STREQUAL "")
  message(SEND_ERROR "layout sync: protocol number not readable (core '${core_proto}', sdk '${sdk_proto_v}')")
elseif(NOT core_proto STREQUAL sdk_proto_v)
  message(SEND_ERROR "layout sync: protocol number is ${core_proto} in the core (kModuleProtocolVersion) and ${sdk_proto_v} in the SDK (kProtocolVersion)")
endif()

# everything else: the whole normalized text (enums with their values, every struct member, the offset helpers, the static_asserts)
if(NOT core_text STREQUAL sdk_text)
  string(LENGTH "${core_text}" core_len)
  string(LENGTH "${sdk_text}" sdk_len)
  if(core_len LESS sdk_len)
    set(lo 0)
    set(hi ${core_len})
  else()
    set(lo 0)
    set(hi ${sdk_len})
  endif()
  # longest common prefix by bisection
  while(lo LESS hi)
    math(EXPR mid "(${lo} + ${hi} + 1) / 2")
    string(SUBSTRING "${core_text}" 0 ${mid} core_head)
    string(SUBSTRING "${sdk_text}" 0 ${mid} sdk_head)
    if(core_head STREQUAL sdk_head)
      set(lo ${mid})
    else()
      math(EXPR hi "${mid} - 1")
    endif()
  endwhile()
  if(lo GREATER 80)
    math(EXPR from "${lo} - 80")
  else()
    set(from 0)
  endif()
  string(SUBSTRING "${core_text}" ${from} 200 core_ctx)
  string(SUBSTRING "${sdk_text}" ${from} 200 sdk_ctx)
  message(SEND_ERROR "layout sync: ModuleTier2Layout.h and the SDK's tier2_layout.h differ at normalized offset ${lo}:\n  core: ...${core_ctx}\n  sdk:  ...${sdk_ctx}")
endif()

# SEND_ERROR continues; stop here with a failing exit status if anything was reported
if(NOT core_text STREQUAL sdk_text OR NOT core_proto STREQUAL sdk_proto_v)
  message(FATAL_ERROR "layout sync FAILED: land a layout change in the core and the SDK together (same kTier2LayoutVersion, same protocol number)")
endif()
message(STATUS "layout sync OK: Tier 2 layout identical to the SDK's (protocol ${core_proto})")
