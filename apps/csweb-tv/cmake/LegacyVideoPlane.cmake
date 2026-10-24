# SPDX-License-Identifier: GPL-3.0-or-later
# Called only for the legacy TV package. The main and host targets retain the kit backend.
if (NOT CSWEB_LEGACY OR NOT WNK_WEBOS)
    return()
endif ()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_legacy_generator "${_csweb_root}/tools/make-legacy-starfish.py")
set(_legacy_generated "${CMAKE_CURRENT_BINARY_DIR}/legacy-video")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_legacy_generator}"
    "${PROJECT_SOURCE_DIR}/media/src/starfish_backend.cpp"
    "${PROJECT_SOURCE_DIR}/media/include/wnk/media/starfish_backend.hpp")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${_legacy_generator}"
    "${PROJECT_SOURCE_DIR}" "${_legacy_generated}" RESULT_VARIABLE _legacy_rc)
if (NOT _legacy_rc EQUAL 0)
    message(FATAL_ERROR "Cannot generate legacy Starfish backend; review the pinned kit integration")
endif ()

add_library(csweb_legacy_starfish STATIC
    "${_legacy_generated}/player/legacy_starfish_backend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/player/legacy_acb.cpp")
target_include_directories(csweb_legacy_starfish PUBLIC
    "${_legacy_generated}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(csweb_legacy_starfish PUBLIC wnk_media PRIVATE wnk_logging ${CMAKE_DL_LIBS})
target_compile_definitions(csweb_player PRIVATE CSWEB_LEGACY_VIDEO_PLANE)
target_link_libraries(csweb_player PUBLIC csweb_legacy_starfish)
