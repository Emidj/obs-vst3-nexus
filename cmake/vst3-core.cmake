# Explicit early-core build, independent of OBS/Qt. No installed plug-ins loaded at build time.
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_MODULE_INFO OFF CACHE BOOL "" FORCE)
set(NEXUS_VST3_SDK "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vst3sdk")
add_subdirectory("${NEXUS_VST3_SDK}" "${CMAKE_CURRENT_BINARY_DIR}/vst3sdk" EXCLUDE_FROM_ALL)
add_library(nexus_vst3_core STATIC src/vst3/keyboard.cpp src/vst3/editor.cpp src/vst3/core.cpp src/vst3/rack_factory.cpp "${NEXUS_VST3_SDK}/public.sdk/source/vst/hosting/module_win32.cpp")
target_include_directories(nexus_vst3_core PUBLIC src)
target_include_directories(nexus_vst3_core SYSTEM PUBLIC "${NEXUS_VST3_SDK}")
target_link_libraries(nexus_vst3_core PUBLIC nexus_dsp sdk_hosting PRIVATE shell32 ole32 user32)
target_compile_definitions(nexus_vst3_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
  target_compile_options(nexus_vst3_core PRIVATE /W4 /WX /permissive-)
endif()

if(BUILD_TESTING)
  add_library(nexus_vst3_fixture MODULE tests/vst3/fixture.cpp "${NEXUS_VST3_SDK}/public.sdk/source/main/dllmain.cpp")
  target_link_libraries(nexus_vst3_fixture PRIVATE sdk user32)
  target_compile_definitions(nexus_vst3_fixture PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  set_target_properties(nexus_vst3_fixture PROPERTIES PREFIX "" SUFFIX ".vst3" OUTPUT_NAME "NexusFixture"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/NexusFixture.vst3/Contents/x86_64-win"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/NexusFixture.vst3/Contents/x86_64-win")
  set(adelay "${NEXUS_VST3_SDK}/public.sdk/samples/vst/adelay/source")
  add_library(nexus_sdk_adelay_fixture MODULE "${adelay}/adelayprocessor.cpp" "${adelay}/adelaycontroller.cpp" "${adelay}/factory.cpp"
    "${NEXUS_VST3_SDK}/public.sdk/source/main/dllmain.cpp")
  target_link_libraries(nexus_sdk_adelay_fixture PRIVATE sdk)
  set_target_properties(nexus_sdk_adelay_fixture PROPERTIES PREFIX "" SUFFIX ".vst3" OUTPUT_NAME "ADelaySDKFixture"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/ADelaySDKFixture.vst3/Contents/x86_64-win"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/ADelaySDKFixture.vst3/Contents/x86_64-win")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixture-version")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/fixture-version/projectversion.h" "#pragma once
#define FULL_VERSION_STR \"3.8.1-internal-fixture\"
")
  target_include_directories(nexus_sdk_adelay_fixture PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/fixture-version")
endif()

if(BUILD_TESTING)
  find_package(Threads REQUIRED)
  add_executable(nexus_vst3_core_tests tests/vst3/core_tests.cpp)
  target_link_libraries(nexus_vst3_core_tests PRIVATE nexus_vst3_core Threads::Threads)
  add_dependencies(nexus_vst3_core_tests nexus_vst3_fixture nexus_sdk_adelay_fixture)
  add_test(NAME vst3_core_audio_state_and_bounds COMMAND nexus_vst3_core_tests
    "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/NexusFixture.vst3"
    "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/ADelaySDKFixture.vst3"
    "${CMAKE_CURRENT_BINARY_DIR}/phase8-vst3-core-report.json")
  set_tests_properties(vst3_core_audio_state_and_bounds PROPERTIES TIMEOUT 90)
endif()

if(NEXUS_BUILD_TEST_BENCH)
  add_executable(nexus_vst3_probe src/vst3/probe.cpp)
  target_link_libraries(nexus_vst3_probe PRIVATE nexus_vst3_core nexus_bench_io)
endif()

# Catalog coordinator deliberately does not link the SDK or DSP core.
add_library(nexus_vst3_catalog STATIC src/vst3/scan_wire.cpp src/vst3/catalog.cpp)
target_include_directories(nexus_vst3_catalog PUBLIC src)
target_compile_definitions(nexus_vst3_catalog PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_link_libraries(nexus_vst3_catalog PRIVATE bcrypt user32 shell32 ole32)
if(MSVC)
  target_compile_options(nexus_vst3_catalog PRIVATE /W4 /WX /permissive- /utf-8)
endif()
add_executable(nexus_vst3_worker src/vst3/scan_worker.cpp)
target_link_libraries(nexus_vst3_worker PRIVATE nexus_vst3_core nexus_vst3_catalog)
add_executable(nexus_vst3_scan src/vst3/scan_cli.cpp)
target_link_libraries(nexus_vst3_scan PRIVATE nexus_vst3_catalog)
add_dependencies(nexus_vst3_scan nexus_vst3_worker)

if(BUILD_TESTING)
  add_executable(nexus_vst3_scan_fake_worker tests/vst3/scan_fake_worker.cpp)
  target_link_libraries(nexus_vst3_scan_fake_worker PRIVATE nexus_vst3_catalog)
  add_library(nexus_vst3_scan_crash_fixture MODULE tests/vst3/scan_bad_plugin.cpp)
  add_library(nexus_vst3_scan_hang_fixture MODULE tests/vst3/scan_bad_plugin.cpp)
  target_compile_definitions(nexus_vst3_scan_hang_fixture PRIVATE NEXUS_HANG_FIXTURE)
  set_target_properties(nexus_vst3_scan_crash_fixture nexus_vst3_scan_hang_fixture PROPERTIES PREFIX "" SUFFIX ".vst3")
  add_executable(nexus_vst3_scanner_tests tests/vst3/scanner_tests.cpp)
  target_link_libraries(nexus_vst3_scanner_tests PRIVATE nexus_vst3_catalog Threads::Threads)
  add_dependencies(nexus_vst3_scanner_tests nexus_vst3_worker nexus_vst3_scan_fake_worker nexus_vst3_fixture nexus_vst3_scan_crash_fixture nexus_vst3_scan_hang_fixture)
  add_test(NAME vst3_scanner_process_cache_and_quarantine COMMAND nexus_vst3_scanner_tests
    $<TARGET_FILE:nexus_vst3_worker> $<TARGET_FILE:nexus_vst3_scan_fake_worker>
    $<TARGET_FILE:nexus_vst3_fixture> $<TARGET_FILE:nexus_vst3_scan_crash_fixture> $<TARGET_FILE:nexus_vst3_scan_hang_fixture> "${CMAKE_CURRENT_BINARY_DIR}/phase8-scanner-report.json")
  set_tests_properties(vst3_scanner_process_cache_and_quarantine PROPERTIES TIMEOUT 90)
endif()

if(BUILD_TESTING)
  add_executable(nexus_vst3_rack_tests tests/vst3/rack_tests.cpp)
  target_link_libraries(nexus_vst3_rack_tests PRIVATE nexus_vst3_core)
  add_dependencies(nexus_vst3_rack_tests nexus_vst3_fixture)
  add_test(NAME vst3_live_rack_integration COMMAND nexus_vst3_rack_tests
    "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/NexusFixture.vst3")
  set_tests_properties(vst3_live_rack_integration PROPERTIES TIMEOUT 90)
endif()

if(BUILD_TESTING)
  add_executable(nexus_vst3_editor_tests tests/vst3/editor_tests.cpp)
  target_link_libraries(nexus_vst3_editor_tests PRIVATE nexus_vst3_core user32)
  target_compile_definitions(nexus_vst3_editor_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  add_dependencies(nexus_vst3_editor_tests nexus_vst3_fixture nexus_sdk_adelay_fixture)
  add_test(NAME vst3_original_editor_native_lifecycle COMMAND nexus_vst3_editor_tests
    "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/NexusFixture.vst3"
    "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/ADelaySDKFixture.vst3")
  set_tests_properties(vst3_original_editor_native_lifecycle PROPERTIES TIMEOUT 30)
endif()

if(BUILD_TESTING)
 add_executable(nexus_vst3_keyboard_tests tests/vst3/keyboard_tests.cpp)
 target_link_libraries(nexus_vst3_keyboard_tests PRIVATE nexus_vst3_core user32)
 target_compile_definitions(nexus_vst3_keyboard_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
 add_dependencies(nexus_vst3_keyboard_tests nexus_vst3_fixture)
 add_test(NAME vst3_editor_keyboard_focus_and_text COMMAND nexus_vst3_keyboard_tests "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/NexusFixture.vst3")
 set_tests_properties(vst3_editor_keyboard_focus_and_text PROPERTIES TIMEOUT 30)
endif()
