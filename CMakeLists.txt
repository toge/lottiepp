cmake_minimum_required(VERSION 3.25)
project(lottiepp VERSION 0.1.0 LANGUAGES C CXX)

option(BUILD_TEST "Enable tests" ON)
option(BUILD_CLI "Build lottieproc CLI" ON)
option(ENABLE_CHART "Build chart/plot sample + helper" ON)

set(STD_CPP cxx_std_23)

## add warning
if (CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g3")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")
endif()

if (MSVC)
    if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
        string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    endif()
endif()

## optimization
if (CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native")
endif()
if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Ox")
endif()

## static option
option(STATIC "static binary" OFF)
if (CMAKE_COMPILER_IS_GNUCXX)
    if(STATIC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static -static-libstdc++ -static-libgcc")
    endif()
endif()

## add LTO flag
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

## other flag
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

## google perf tools
# HEAPPROFILE=heap CPUPROFILE=test.prof xxx
# pprof --text xxx yyy.prof
#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lprofiler -ltcmalloc")

find_package(glaze REQUIRED CONFIG)
find_package(miniz REQUIRED CONFIG)
find_package(Catch2 CONFIG QUIET)

# --- lottiepp library (static) ---
add_library(lottiepp STATIC
    src/lottiepp.cpp
)
target_include_directories(lottiepp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(lottiepp PUBLIC glaze::glaze miniz::miniz)
target_compile_features(lottiepp PUBLIC ${STD_CPP})

# --- lottieproc CLI ---
if(BUILD_CLI)
    add_executable(lottieproc src/main.cpp)
    target_link_libraries(lottieproc PRIVATE lottiepp)
    target_compile_features(lottieproc PRIVATE ${STD_CPP})
endif()

if(BUILD_TEST AND Catch2_FOUND)
    enable_testing()
    add_subdirectory(test)
elseif(BUILD_TEST AND NOT Catch2_FOUND)
    message(STATUS "Catch2 not found; tests disabled")
endif()

# --- chart/plot helper (ENABLE_CHART=ON) ---
if(ENABLE_CHART)
    # プロット機能を lottiepp ライブラリに統合してビルドする
    target_sources(lottiepp PRIVATE src/chart.cpp)


    target_include_directories(lottiepp PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)

    # サンプル実行ファイル
    add_executable(line_chart_grow samples/line_chart_grow.cpp)
    target_link_libraries(line_chart_grow PRIVATE lottiepp)
    target_compile_features(line_chart_grow PRIVATE ${STD_CPP})
endif()

# ---------------------------------------------------------------------------
# Install + CMake package config
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(LOTTIPP_TARGETS_EXPORT lottieppTargets)
set(LOTTIPP_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/lottiepp"
    CACHE PATH "CMake package config location")

install(TARGETS lottiepp
    EXPORT ${LOTTIPP_TARGETS_EXPORT}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES src/lottiepp.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_CLI)
    install(TARGETS lottieproc
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(EXPORT ${LOTTIPP_TARGETS_EXPORT}
    NAMESPACE lottiepp::
    DESTINATION ${LOTTIPP_INSTALL_CMAKEDIR}
    FILE ${LOTTIPP_TARGETS_EXPORT}.cmake
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/lottieppConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/lottieppConfig.cmake
    INSTALL_DESTINATION ${LOTTIPP_INSTALL_CMAKEDIR}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/lottieppConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/lottieppConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/lottieppConfigVersion.cmake
    DESTINATION ${LOTTIPP_INSTALL_CMAKEDIR}
)

# ---------------------------------------------------------------------------
# CPack installer
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME lottiepp)
set(CPACK_PACKAGE_VENDOR "lottiepp")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Lottie (.json/.lottie) editing library and CLI")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
set(CPACK_VERBATIM_VARIABLES ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CPACK_GENERATOR NSIS)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(CPACK_GENERATOR productbuild)
else()
    set(CPACK_GENERATOR DEB)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
endif()
include(CPack)
