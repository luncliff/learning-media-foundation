cmake_minimum_required(VERSION 3.20)
project(learning-media-foundation VERSION 0.1.3 LANGUAGES CXX)

option(BUILD_SHARED_LIBS "https://cmake.org/cmake/help/latest/variable/BUILD_SHARED_LIBS.html" ON)
option(BUILD_TESTING "https://cmake.org/cmake/help/latest/module/CTest.html" ON)

set(CMAKE_SUPPRESS_REGENERATION true)
set(CMAKE_INSTALL_DEBUG_LIBRARIES true)
set(CMAKE_INSTALL_UCRT_LIBRARIES true)
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

# see https://github.com/microsoft/onnxruntime/tree/master/cmake
include(winml_sdk_helpers)
include(winml_cppwinrt)
get_sdk(sdk_folder sdk_version)
get_sdk_include_folder(${sdk_folder} ${sdk_version} sdk_include_folder)
message(STATUS "Windows SDK")
message(STATUS "  ${sdk_version}") # expect CMAKE_SYSTEM_VERSION
message(STATUS "  ${sdk_folder}")
message(STATUS "  ${sdk_include_folder}")

include(InstallRequiredSystemLibraries)
include(GNUInstallDirs)
include(CheckIncludeFileCXX)
# see https://docs.microsoft.com/en-us/windows/win32/medfound/media-foundation-headers-and-libraries
check_include_file_cxx("d3d11_4.h" found_d3d11)
check_include_file_cxx("dxgi1_6.h" found_dxgi)
check_include_file_cxx("mfapi.h"   found_mfapi)
check_include_file_cxx("wincodecsdk.h" found_wincodecsdk)

if(CMAKE_CXX_COMPILER_ID MATCHES Clang)
    message(FATAL_ERROR "This project uses WinRT. clang-cl can't be used since <experimentatl/coroutine> is not supported anymore")
endif()
include(CheckCXXCompilerFlag)

find_package(Git REQUIRED)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_ID
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "Using commit: ${GIT_COMMIT_ID}")

string(TIMESTAMP BUILD_TIMESTAMP UTC) # %Y-%m-%dT%H:%M:%SZ
message(STATUS "Using timestamp: ${BUILD_TIMESTAMP}")

list(APPEND BUILD_COMPILE_DEFINITIONS
    SOURCE_VERSION="${PROJECT_VERSION}"
    SOURCE_COMMIT_ID="${GIT_COMMIT_ID}"
    SOURCE_TIMESTAMP="${BUILD_TIMESTAMP}"
)

find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_package(directxmath CONFIG REQUIRED)

target_cppwinrt(media_test_api_codegen
    ${PROJECT_SOURCE_DIR}/test/MessageHolder.idl MessageHolder
    codegen "${sdk_folder}" "${sdk_version}"
    codegen "" true ""
)

add_library(media_test_api SHARED
    test/WinRTComponent.def
    # test/MessageHolder.idl
    test/WinRTComponent.MessageHolder.h
    test/WinRTComponent.MessageHolder.cpp
    test/dllmain.cpp
)
add_dependencies(media_test_api media_test_api_codegen)

set_target_properties(media_test_api
PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS true
)

target_include_directories(media_test_api
PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/media_test_api_codegen/comp_generated>
PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/media_test_api_codegen
    ${CMAKE_CURRENT_BINARY_DIR}/media_test_api_codegen/comp_generated/WinRTComponent
    test
)

target_compile_definitions(media_test_api
PRIVATE
    WIN32_LEAN_AND_MEAN
    _WINRT_DLL _WINDLL
)

set_source_files_properties(test/dllmain.cpp
PROPERTIES
    COMPILE_DEFINITIONS "${BUILD_COMPILE_DEFINITIONS}"
)

target_compile_options(media_test_api
PRIVATE
    /std:c++17 /Zc:__cplusplus /await
    /W4 /bigobj
)

target_link_libraries(media_test_api
PRIVATE
    windowsapp # C++/WinRT
)

add_executable(media_test_suite
    test/test_main.cpp
    test/test_dxva.cpp
    test/mf_transform.hpp
    test/mf_transform.cpp
    test/test_mf_transform.cpp
    test/test_mf_threading.cpp
    test/string.cpp
)

target_compile_definitions(media_test_suite
PRIVATE
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(media_test_suite
PRIVATE
    media_test_api
    windowsapp # C++/WinRT
    mf mfplat mfplay mfreadwrite mfuuid wmcodecdspuuid # Media Foundation SDK
    shlwapi comctl32 # Windows COM
    strmiids dxva2 evr d3d11 dxguid dxgi # DXVA
    fmt::fmt-header-only spdlog::spdlog_header_only
    Microsoft::DirectX-Headers
    Microsoft::DirectXTex
    Microsoft::DirectXTK
    Microsoft::DirectXMath
)

target_compile_options(media_test_suite
PRIVATE
    /wd4819 # codepage
    /wd4651 # macro in precompiled header
)

target_compile_options(media_test_suite
PRIVATE
    /std:c++17 /Zc:__cplusplus /await
    /W4 /bigobj
)

target_link_options(media_test_suite
PRIVATE
    /ERRORREPORT:SEND
)

set_source_files_properties(test/test_main.cpp
PROPERTIES
    COMPILE_DEFINITIONS "${BUILD_COMPILE_DEFINITIONS}"
)

install(TARGETS media_test_suite
        DESTINATION ${CMAKE_INSTALL_BINDIR}
)

set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "luncliff@gmail.com")
set(CPACK_PACKAGE_DESCRIPTION "https://github.com/luncliff/learning-media-foundation")
set(CPACK_RESOURCE_FILE_README ${PROJECT_SOURCE_DIR}/readme.md)
set(CPACK_GENERATOR "ZIP")
include(CPack)

find_package(glfw3 CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)
include(Catch)

target_include_directories(media_test_suite
PRIVATE
    $<$<CONFIG:Debug>:${CMAKE_CURRENT_BINARY_DIR}/media.dir/Debug>
    $<$<CONFIG:Release>:${CMAKE_CURRENT_BINARY_DIR}/media.dir/Release>
    $<$<CONFIG:RelWithDebInfo>:${CMAKE_CURRENT_BINARY_DIR}/media.dir/RelWithDebInfo>
    $<$<CONFIG:MinSizeRel>:${CMAKE_CURRENT_BINARY_DIR}/media.dir/MinSizeRel>
)

target_link_libraries(media_test_suite
PRIVATE
    Catch2::Catch2 glfw
)

get_filename_component(ASSET_DIR    ${PROJECT_SOURCE_DIR}/assets ABSOLUTE)
target_compile_definitions(media_test_suite
PRIVATE
    ASSET_DIR="${ASSET_DIR}"
    UNICODE
    CATCH_CONFIG_FAST_COMPILE
    CATCH_CONFIG_WCHAR
    CATCH_CONFIG_WINDOWS_CRTDBG
)

catch_discover_tests(media_test_suite)
