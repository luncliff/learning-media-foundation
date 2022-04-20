cmake_minimum_required(VERSION 3.20)
project(windows-demo VERSION 1.0.0 LANGUAGES CXX)

option(BUILD_SHARED_LIBS "https://cmake.org/cmake/help/latest/variable/BUILD_SHARED_LIBS.html" ON)
option(BUILD_TESTING "https://cmake.org/cmake/help/latest/module/CTest.html" ON)

set(CMAKE_SUPPRESS_REGENERATION true)
set(CMAKE_INSTALL_DEBUG_LIBRARIES true)
set(CMAKE_INSTALL_UCRT_LIBRARIES true)
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

# see https://github.com/microsoft/onnxruntime/tree/master/cmake
include(winml_sdk_helpers)
get_sdk(sdk_folder sdk_version)
get_sdk_include_folder(${sdk_folder} ${sdk_version} sdk_include_folder)
message(STATUS "Windows SDK")
message(STATUS "  ${sdk_version}") # expect CMAKE_SYSTEM_VERSION
message(STATUS "  ${sdk_folder}")
message(STATUS "  ${sdk_include_folder}")

include(InstallRequiredSystemLibraries)
include(GNUInstallDirs)
include(CheckIncludeFileCXX)
include(CheckCXXCompilerFlag)

find_package(Git REQUIRED)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_ID
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "Using commit: ${GIT_COMMIT_ID}")

list(APPEND BUILD_COMPILE_DEFINITIONS
    SOURCE_VERSION="${PROJECT_VERSION}"
    SOURCE_COMMIT_ID="${GIT_COMMIT_ID}"
)

find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)

add_executable(media_test_suite)

target_sources(media_test_suite
PRIVATE
    test/test_main.cpp
)

target_compile_definitions(media_test_suite
PRIVATE
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(media_test_suite
PRIVATE
    windowsapp # C++/WinRT
    # shlwapi comctl32 # Windows COM
    # strmiids dxva2 evr d3d11 dxguid dxgi # DXVA
    fmt::fmt-header-only spdlog::spdlog_header_only
    Microsoft::DirectX-Headers
    Microsoft::DirectXTex
    Microsoft::DirectXTK
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
set(CPACK_PACKAGE_VENDOR "dong-ha-park@linecorp.com")
set(CPACK_PACKAGE_DESCRIPTION "https://git.linecorp.com/luncliff")
set(CPACK_RESOURCE_FILE_README ${PROJECT_SOURCE_DIR}/readme.md)
set(CPACK_GENERATOR "ZIP")
include(CPack)

find_package(Catch2 CONFIG REQUIRED)
include(Catch)

target_link_libraries(media_test_suite
PRIVATE
    Catch2::Catch2
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
