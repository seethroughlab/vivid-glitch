cmake_minimum_required(VERSION 3.20)
project(vivid-glitch VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Locate vivid source and build trees ---
# VIVID_SRC_DIR: path to vivid source tree (for operator_api headers, linmath.h)
# VIVID_BUILD_DIR: path to vivid build tree (for WebGPU)
if(NOT DEFINED VIVID_SRC_DIR)
    set(VIVID_SRC_DIR "${CMAKE_SOURCE_DIR}/../vivid")
endif()
if(NOT DEFINED VIVID_BUILD_DIR)
    set(VIVID_BUILD_DIR "${VIVID_SRC_DIR}/build")
endif()

# Reserved for future package tests; keep OFF for install/rebuild speed.
option(VIVID_PACKAGE_BUILD_TESTS "Build vivid-glitch package tests" OFF)

# --- Operator API (header-only) ---
add_library(vivid_operator_api INTERFACE)
target_include_directories(vivid_operator_api INTERFACE
    ${VIVID_SRC_DIR}/src            # operator_api/*.h
    ${VIVID_SRC_DIR}/deps/glfw/deps # linmath.h
)

# --- WebGPU discovery ---
# Find the wgpu include/lib directories in the vivid build tree
set(_wgpu_include "")
set(_wgpu_lib_dir "")
set(_deps_dir "${VIVID_BUILD_DIR}/_deps")
if(IS_DIRECTORY "${_deps_dir}")
    file(GLOB _dep_entries "${_deps_dir}/*")
    foreach(_entry ${_dep_entries})
        get_filename_component(_entry_name "${_entry}" NAME)
        if(_entry_name MATCHES "wgpu.*-src")
            if(EXISTS "${_entry}/include/webgpu/webgpu.h")
                set(_wgpu_include "${_entry}/include")
                if(IS_DIRECTORY "${_entry}/lib")
                    set(_wgpu_lib_dir "${_entry}/lib")
                endif()
            endif()
        endif()
    endforeach()
endif()

# Fallback: SDK include in vivid source dir
if(_wgpu_include STREQUAL "")
    if(EXISTS "${VIVID_SRC_DIR}/include/webgpu/webgpu.h")
        set(_wgpu_include "${VIVID_SRC_DIR}/include")
    endif()
endif()
# Fallback: libwgpu_native next to the build dir
if(_wgpu_lib_dir STREQUAL "")
    if(EXISTS "${VIVID_BUILD_DIR}/libwgpu_native.dylib")
        set(_wgpu_lib_dir "${VIVID_BUILD_DIR}")
    endif()
endif()

add_library(webgpu INTERFACE)
if(NOT _wgpu_include STREQUAL "")
    target_include_directories(webgpu INTERFACE "${_wgpu_include}")
endif()
if(NOT _wgpu_lib_dir STREQUAL "")
    find_library(WGPU_NATIVE_LIB wgpu_native PATHS "${_wgpu_lib_dir}" NO_DEFAULT_PATH)
    if(WGPU_NATIVE_LIB)
        target_link_libraries(webgpu INTERFACE "${WGPU_NATIVE_LIB}")
    endif()
endif()

# --- Platform plugin suffix ---
if(APPLE)
    set(VIVID_PLUGIN_SUFFIX ".dylib")
elseif(WIN32)
    set(VIVID_PLUGIN_SUFFIX ".dll")
else()
    set(VIVID_PLUGIN_SUFFIX ".so")
endif()

# --- Audio operators (no GPU dependency) ---
foreach(_op stutter tape_stop beat_repeat reverse scratch stretch freq_shift glitch)
    add_library(${_op} MODULE operators/audio/${_op}/${_op}.cpp)
    set_target_properties(${_op} PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")
    target_link_libraries(${_op} PRIVATE vivid_operator_api)
    target_include_directories(${_op} PRIVATE ${CMAKE_SOURCE_DIR}/operators/audio/glitch_common)
endforeach()

# --- GPU operators ---
foreach(_op jpeg_glitch pixel_sort block_displacement scan_distort channel_shift vhs static_glitch datamosh visual_glitch)
    add_library(${_op} MODULE operators/gpu/${_op}/${_op}.cpp)
    set_target_properties(${_op} PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")
    target_link_libraries(${_op} PRIVATE vivid_operator_api webgpu)
    target_include_directories(${_op} PRIVATE ${CMAKE_SOURCE_DIR}/operators/gpu/glitch_common)
endforeach()

# --- Operators meta-target (for smoke CI: builds only dylibs) ---
add_custom_target(operators DEPENDS
    stutter tape_stop beat_repeat reverse scratch stretch freq_shift glitch
    jpeg_glitch pixel_sort block_displacement scan_distort channel_shift
    vhs static_glitch datamosh visual_glitch)

# --- Copy demo graphs to build directory ---
file(GLOB _GLITCH_GRAPHS graphs/*.json)
foreach(g ${_GLITCH_GRAPHS})
    get_filename_component(name ${g} NAME)
    configure_file(${g} ${CMAKE_BINARY_DIR}/graphs/${name} COPYONLY)
endforeach()

if(VIVID_PACKAGE_BUILD_TESTS)
    enable_testing()
    add_executable(test_rate_utils tests/cpp/test_rate_utils.cpp)
    target_link_libraries(test_rate_utils PRIVATE vivid_operator_api)
    add_test(NAME test_rate_utils COMMAND test_rate_utils)
endif()
