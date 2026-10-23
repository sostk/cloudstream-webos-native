# SPDX-License-Identifier: GPL-3.0-or-later
# CSWeb TV, the native app (apps/csweb-tv/README.md). Built inside the kit's tree through
# WNK_EXTRA_APP_DIRS (the kit's cmake/ExtraApps.cmake).

set(CSWEB_APP_ID org.csweb.tv)
set(CSWEB_TITLE "CSWeb TV")
# The legacy package (docs/legacy-build.md): TVs whose kernel is 32-bit (uname armv7l) cannot start the
# aarch64 plugin runtime, and firmware before webOS 5.3.1 has no libcurl. With this ON the package is
# org.csweb.tv.legacy, carries runtime-armv7 and its own libcurl. OFF (default) builds exactly the
# main package.
option(CSWEB_LEGACY "Build the legacy package (armv7 runtime, bundled libcurl) for 32-bit-kernel TVs" OFF)
if (CSWEB_LEGACY)
    set(CSWEB_APP_ID org.csweb.tv.legacy)
    set(CSWEB_TITLE "CSWeb TV Legacy")
endif ()
set(CSWEB_VERSION 0.1.0 CACHE STRING "App version, three integers (the release workflow sets it)")
get_filename_component(_csweb_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# The kit remains pinned and unmodified. Only the legacy SDL source build gets
# this third-party guard, after the kit's own SDL patch and before configure.
if (CSWEB_LEGACY AND WNK_WEBOS)
    set_property(TARGET wnk_sdl_webos_ext PROPERTY CSWEB_LEGACY_GUARD_SCRIPT
        "${_csweb_root}/tools/patch-legacy-sdl.py")
    function(csweb_legacy_guard_sdl)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        ExternalProject_Get_Property(wnk_sdl_webos_ext SOURCE_DIR)
        get_property(_guard TARGET wnk_sdl_webos_ext PROPERTY CSWEB_LEGACY_GUARD_SCRIPT)
        ExternalProject_Add_Step(wnk_sdl_webos_ext csweb_legacy_foreign_guard
            COMMAND "${Python3_EXECUTABLE}" "${_guard}" "${SOURCE_DIR}"
            DEPENDEES patch
            DEPENDERS configure
            DEPENDS "${_guard}"
            COMMENT "Guard legacy SDL video export when wl_webos_foreign is unavailable")
    endfunction()
    # ExternalProject's stamp commands belong to the kit's root directory.
    # Adding the step from this app subdirectory cannot append to those commands.
    cmake_language(DEFER DIRECTORY "${PROJECT_SOURCE_DIR}" CALL csweb_legacy_guard_sdl)
endif ()

# ---- csweb_core: no UI; runtime client, plugins, persistence, utilities ---------------------------
find_package(Threads REQUIRED)
add_library(csweb_core STATIC
        src/app/deep_link.cpp
        src/core/child_process.cpp
        src/core/dir_snapshot.cpp
        src/core/http_url.cpp
        src/core/sha256.cpp
        src/core/stall_sampler.cpp
        src/core/zip_check.cpp
        src/library/library.cpp
        src/library/library_store.cpp
        src/plugins/plugin_data.cpp
        src/plugins/plugin_manager.cpp
        src/plugins/plugin_store.cpp
        src/plugins/repository.cpp
        src/profiles/profiles.cpp
        src/settings/preferences.cpp
        src/settings/settings_store.cpp
        src/runtime/episode_groups.cpp
        src/runtime/provider_test.cpp
        src/runtime/runtime_client.cpp
        src/runtime/runtime_launcher.cpp
        src/runtime/runtime_types.cpp)
target_include_directories(csweb_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(csweb_core SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/third_party")
target_link_libraries(csweb_core PUBLIC wnk_storage wnk_logging wnk_net wnk_async Threads::Threads)
if (CSWEB_LEGACY)
    target_compile_definitions(csweb_core PUBLIC CSWEB_LEGACY)
endif ()

# ---- tests (host) -----------------------------------------------------------------------------------
if (WNK_BUILD_TESTS)
    foreach (t sha256 zip_check repository runtime_types plugin_store runtime_client episode_groups redact deep_link library dir_snapshot preferences provider_test profiles http_url)
        add_executable(csweb_test_${t} tests/test_${t}.cpp)
        target_include_directories(csweb_test_${t} PRIVATE "${PROJECT_SOURCE_DIR}/tests/unit")
        target_compile_definitions(csweb_test_${t} PRIVATE CSWEB_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
        target_link_libraries(csweb_test_${t} PRIVATE csweb_core)
        add_test(NAME csweb.${t} COMMAND csweb_test_${t})
    endforeach ()
    # Compile the launcher independently in each mode so legacy-only environment changes are covered.
    foreach (mode main legacy)
        add_executable(csweb_test_runtime_launcher_${mode}
                tests/test_runtime_launcher.cpp src/runtime/runtime_launcher.cpp)
        target_include_directories(csweb_test_runtime_launcher_${mode} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests/unit")
        target_include_directories(csweb_test_runtime_launcher_${mode} SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party")
        if (mode STREQUAL "legacy")
            target_compile_definitions(csweb_test_runtime_launcher_${mode} PRIVATE CSWEB_LEGACY)
        endif ()
        add_test(NAME csweb.runtime_launcher_${mode} COMMAND csweb_test_runtime_launcher_${mode})
        add_executable(csweb_test_child_process_log_${mode}
                tests/test_child_process_log.cpp src/core/child_process.cpp)
        target_include_directories(csweb_test_child_process_log_${mode} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests/unit")
        target_link_libraries(csweb_test_child_process_log_${mode} PRIVATE Threads::Threads)
        if (mode STREQUAL "legacy")
            target_compile_definitions(csweb_test_child_process_log_${mode} PRIVATE CSWEB_LEGACY)
        endif ()
        add_test(NAME csweb.child_process_log_${mode} COMMAND csweb_test_child_process_log_${mode})
    endforeach ()
endif ()

# ---- csweb_player: link choice, subtitle files, the kit media pipeline for a link ----------------------
if (NOT TARGET wnk_media_ffmpeg)
    message(STATUS "CSWeb TV skipped: configure with WNK_FFMPEG=ON (the player needs FFmpeg)")
    return()
endif ()
add_library(csweb_player STATIC
        src/player/link_priority.cpp
        src/player/media_factory.cpp
        src/player/merged_source.cpp
        src/player/subtitle_file.cpp)
target_link_libraries(csweb_player PUBLIC csweb_core wnk_media wnk_media_ffmpeg wnk_media_ndl wnk_media_starfish)
if (CSWEB_LEGACY AND WNK_WEBOS)
    include(cmake/LegacyVideoPlane.cmake)
endif ()

# ---- csweb-tv: the app ------------------------------------------------------------------------------
add_executable(csweb-tv
        src/main.cpp
        src/app/application.cpp
        src/app/router.cpp
        src/ui/dialogs/multi_select.cpp
        src/ui/dialogs/plugin_dialog_host.cpp
        src/ui/screens/details_screen.cpp
        src/ui/screens/extensions_screen.cpp
        src/ui/screens/home_screen.cpp
        src/ui/screens/library_screen.cpp
        src/ui/screens/player_screen.cpp
        src/ui/screens/profiles_screen.cpp
        src/ui/screens/provider_test_screen.cpp
        src/ui/screens/search_screen.cpp
        src/ui/screens/settings_screen.cpp
        src/ui/screens/setup_screen.cpp
        src/ui/screens/splash_screen.cpp
        src/ui/theme/theme.cpp
        src/ui/widgets/art.cpp
        src/ui/widgets/episode_row.cpp
        src/ui/widgets/glass.cpp
        src/ui/widgets/icons.cpp
        src/ui/widgets/nav_bar.cpp
        src/ui/widgets/pill_button.cpp
        src/ui/widgets/pin_pad.cpp
        src/ui/widgets/profile_avatar.cpp
        src/ui/widgets/top_bar.cpp
        src/ui/widgets/type_chips.cpp)
target_link_libraries(csweb-tv PRIVATE csweb_core csweb_player wnk_app wnk_media_ui)
target_compile_definitions(csweb-tv PRIVATE CSWEB_VERSION="${CSWEB_VERSION}")
# Host runs find the runtime in the repository (runtime/make_dist.py); the TV uses the packaged one.
if (NOT WNK_WEBOS)
    execute_process(COMMAND uname -m OUTPUT_VARIABLE _csweb_arch OUTPUT_STRIP_TRAILING_WHITESPACE)
    target_compile_definitions(csweb-tv PRIVATE CSWEB_HOST_RUNTIME_DIR="${_csweb_root}/runtime/dist/runtime-${_csweb_arch}")
endif ()

set(_csweb_fonts "${PROJECT_SOURCE_DIR}/assets/fonts")
set(_csweb_icons "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons")
set(_csweb_brand "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand")  # tools/make_brand.py
add_custom_command(TARGET csweb-tv POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_csweb_fonts}" "$<TARGET_FILE_DIR:csweb-tv>/fonts"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_csweb_icons}" "$<TARGET_FILE_DIR:csweb-tv>/icons"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_csweb_brand}" "$<TARGET_FILE_DIR:csweb-tv>/brand"
        VERBATIM)

set(_csweb_runtimes)
set(_csweb_bundled_libs ${WNK_FFMPEG_RUNTIME_LIBS})
set(_csweb_extra_dirs "${_csweb_fonts}" "${_csweb_icons}" "${_csweb_brand}")
if (NOT CSWEB_LEGACY)
    if (WNK_WEBOS AND EXISTS "${_csweb_root}/runtime/dist/runtime-aarch64/lib/ld-musl-aarch64.so.1")
        list(APPEND _csweb_runtimes "${_csweb_root}/runtime/dist/runtime-aarch64")
    elseif (WNK_WEBOS)
        message(WARNING "CSWeb TV: runtime/dist/runtime-aarch64 missing; the package will not run plugins")
    endif ()
elseif (WNK_WEBOS)
    target_sources(csweb-tv PRIVATE src/app/legacy_curl_diagnostics.cpp)
    target_compile_definitions(csweb-tv PRIVATE CSWEB_LEGACY_CURL_DIAGNOSTICS)
    target_link_libraries(csweb-tv PRIVATE ${CMAKE_DL_LIBS})
    # Legacy package: the armv7 runtime (tools/build-runtime.sh --arch armv7) ...
    if (EXISTS "${_csweb_root}/runtime/dist/runtime-armv7/lib/ld-musl-armhf.so.1")
        list(APPEND _csweb_runtimes "${_csweb_root}/runtime/dist/runtime-armv7")
    else ()
        message(WARNING "CSWeb TV Legacy: runtime/dist/runtime-armv7 missing; the package will not run plugins")
    endif ()
    # ... and libcurl (tools/build-libcurl-webos.sh), verifying against that runtime's PEM bundle where
    # webOS installs the app (developer mode and Homebrew Channel alike).
    set(CSWEB_LEGACY_INSTALL_DIR "/media/developer/apps/usr/palm/applications/${CSWEB_APP_ID}" CACHE STRING
            "Where webOS installs the legacy package (the bundled libcurl's default CA bundle is under it)")
    set(_csweb_curl "${CMAKE_BINARY_DIR}/csweb-libcurl")
    get_filename_component(_csweb_ndk "${WNK_NDK_PREFIX}" DIRECTORY)  # .../bin
    get_filename_component(_csweb_ndk "${_csweb_ndk}" DIRECTORY)
    message(STATUS "CSWeb TV Legacy: bundled libcurl in ${_csweb_curl}")
    execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "WEBOS_NDK=${_csweb_ndk}" "${_csweb_root}/tools/build-libcurl-webos.sh"
                    "${_csweb_curl}" "${CSWEB_LEGACY_INSTALL_DIR}/runtime-armv7/trust/ca-bundle.pem"
            RESULT_VARIABLE _csweb_curl_rc OUTPUT_VARIABLE _csweb_curl_out ERROR_VARIABLE _csweb_curl_err)
    if (NOT _csweb_curl_rc EQUAL 0)
        message(FATAL_ERROR "tools/build-libcurl-webos.sh failed:\n${_csweb_curl_out}\n${_csweb_curl_err}")
    endif ()
    list(APPEND _csweb_bundled_libs "${_csweb_curl}/lib/libcurl.so.4")
    list(APPEND _csweb_extra_dirs "${_csweb_curl}/licenses-libcurl")
endif ()

wnk_add_webos_package(
        TARGET csweb-tv
        APPID ${CSWEB_APP_ID}
        TITLE "${CSWEB_TITLE}"
        VERSION ${CSWEB_VERSION}
        VENDOR "cloudstream-webos"
        ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/icon.png"
        LARGE_ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/largeIcon.png"
        ICON_COLOR "#0B0D12"
        EXPECTED_NEEDED "${CMAKE_CURRENT_SOURCE_DIR}/packaging/expected-needed.txt"
        EXTRA_DIRS ${_csweb_extra_dirs}
        REQUIRED_PERMISSIONS database.operation
        BUNDLED_LIBS ${_csweb_bundled_libs}
        PRIVATE_RUNTIMES ${_csweb_runtimes}
        SOURCE_URL "https://github.com/sostk/cloudstream-webos-native")

if (WNK_BUILD_TESTS)
    # Host tool: a stream URL through the player's source (tools/media_probe.cpp).
    add_executable(csweb-media-probe tools/media_probe.cpp)
    target_link_libraries(csweb-media-probe PRIVATE csweb_player)
    add_executable(csweb_test_glass tests/test_glass.cpp src/ui/widgets/glass.cpp src/ui/theme/theme.cpp)
    target_include_directories(csweb_test_glass PRIVATE "${PROJECT_SOURCE_DIR}/tests/unit" "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(csweb_test_glass PRIVATE wnk_app)
    add_test(NAME csweb.glass COMMAND csweb_test_glass)
    foreach (t link_priority merged_source subtitle_file)
        add_executable(csweb_test_${t} tests/test_${t}.cpp)
        target_include_directories(csweb_test_${t} PRIVATE "${PROJECT_SOURCE_DIR}/tests/unit")
        target_link_libraries(csweb_test_${t} PRIVATE csweb_player)
        add_test(NAME csweb.${t} COMMAND csweb_test_${t})
    endforeach ()
endif ()
