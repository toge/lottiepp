cmake_minimum_required(VERSION 3.25)
project(lottiepp VERSION 0.1.0 LANGUAGES C CXX)

option(BUILD_TEST "Enable tests" ON)
option(BUILD_CLI "Build lottieproc CLI" ON)
option(ENABLE_CHART "Build the chart/plot helper (samples)" ON)
option(ENABLE_FREESTANDING "Build freestanding wasm32-unknown-unknown library (-nostdlib)" OFF)

if(ENABLE_FREESTANDING)
    message(STATUS "ENABLE_FREESTANDING: freestanding wasm32 build (CLI / chart / tests / samples disabled)")
    set(BUILD_CLI OFF)
    set(ENABLE_CHART OFF)
    set(BUILD_TEST OFF)
endif()

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

## add LTO flag (freestanding wasm ビルドでは無効)
if(NOT ENABLE_FREESTANDING)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT error)
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
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
if(NOT ENABLE_FREESTANDING)
    find_package(miniz REQUIRED CONFIG)
endif()
find_package(Catch2 CONFIG QUIET)

if(ENABLE_FREESTANDING)
    # --- freestanding (wasm32-unknown-unknown, -nostdlib) ビルド設定 ---
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "ENABLE_FREESTANDING=ON requires clang (emsdk clang). e.g. -DCMAKE_CXX_COMPILER=$ENV{HOME}/vm/emsdk/upstream/bin/clang")
    endif()
    set(LOTTIEPP_EMSDK "$ENV{EMSDK}")
    if(NOT LOTTIEPP_EMSDK)
        set(LOTTIEPP_EMSDK "$ENV{HOME}/vm/emsdk")
    endif()
    set(LOTTIEPP_WASM_SYSROOT "${LOTTIEPP_EMSDK}/upstream/emscripten/cache/sysroot")
    if(NOT EXISTS "${LOTTIEPP_WASM_SYSROOT}/include/c++/v1")
        message(FATAL_ERROR "libc++ headers for wasm not found: ${LOTTIEPP_WASM_SYSROOT} (set EMSDK env var)")
    endif()
    set(LOTTIEPP_WASM_FLAGS
        --target=wasm32-unknown-unknown
        -nostdlib
        -nostdinc++
        -fno-exceptions
        -fno-rtti
        -Oz
        -ffunction-sections
        -fdata-sections
    )
    # wasm-ld は --dependency-file 非対応のため CMake のリンカ依存ファイル生成を無効化する
    set(CMAKE_LINK_DEPENDS_USE_LINKER OFF)
    set(LOTTIEPP_WASM_INCLUDES
        "${LOTTIEPP_WASM_SYSROOT}/include/c++/v1"
        "${LOTTIEPP_WASM_SYSROOT}/include"
    )
    # -fno-exceptions 向けにビルド済みの libc++ / libc++abi（extern template 等の実体提供用）と
    # compiler-rt builtins（__multi3 等の 128bit 演算ヘルパ用）
    set(LOTTIEPP_WASM_LIBCXX
        "${LOTTIEPP_WASM_SYSROOT}/lib/wasm32-emscripten/libc++-noexcept.a"
        "${LOTTIEPP_WASM_SYSROOT}/lib/wasm32-emscripten/libc++abi-noexcept.a"
        "${LOTTIEPP_WASM_SYSROOT}/lib/wasm32-emscripten/libclang_rt.builtins.a"
    )
endif()

# --- lottiepp library (static) ---
add_library(lottiepp STATIC
    src/lottiepp.cpp
)
target_include_directories(lottiepp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
if(ENABLE_FREESTANDING)
    target_include_directories(lottiepp SYSTEM PRIVATE ${LOTTIEPP_WASM_INCLUDES})
    target_compile_options(lottiepp PRIVATE ${LOTTIEPP_WASM_FLAGS})
    target_compile_definitions(lottiepp PRIVATE LOTTIEPP_FREESTANDING=1)
    # freestanding では miniz（ZIP 処理）を除外し、glaze（ヘッダオンリー）のみ使用する
    target_link_libraries(lottiepp PUBLIC glaze::glaze)
else()
    target_link_libraries(lottiepp PUBLIC glaze::glaze miniz::miniz)
endif()
target_compile_features(lottiepp PUBLIC ${STD_CPP})

if(ENABLE_FREESTANDING)
    # --- freestanding 動作確認用スモークテスト（wasm 実行バイナリ、要 node） ---
    add_executable(lottiepp_smoke
        test/freestanding_smoke.cpp
        test/freestanding_runtime.cpp
    )
    target_include_directories(lottiepp_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_include_directories(lottiepp_smoke SYSTEM PRIVATE ${LOTTIEPP_WASM_INCLUDES})
    target_compile_options(lottiepp_smoke PRIVATE ${LOTTIEPP_WASM_FLAGS})
    target_compile_definitions(lottiepp_smoke PRIVATE LOTTIEPP_FREESTANDING=1)
    target_compile_features(lottiepp_smoke PRIVATE ${STD_CPP})
    target_link_libraries(lottiepp_smoke PRIVATE lottiepp ${LOTTIEPP_WASM_LIBCXX})
    target_link_options(lottiepp_smoke PRIVATE
        --target=wasm32-unknown-unknown
        -nostdlib
        -Wl,--no-entry
        -Wl,--export=run
        -Wl,--export-memory
        -Wl,--export=g_lottieTrapMsg
        -Wl,--export=g_lottieTrapLen
    )
    set_target_properties(lottiepp_smoke PROPERTIES SUFFIX ".wasm")

    find_program(LOTTIEPP_NODE_EXECUTABLE node)
    if(LOTTIEPP_NODE_EXECUTABLE)
        enable_testing()
        add_test(NAME freestanding_smoke
            COMMAND ${LOTTIEPP_NODE_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/test/freestanding_smoke.js
                    $<TARGET_FILE:lottiepp_smoke>
        )
    endif()
endif()

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

# --- chart/plot helper ---
if(ENABLE_CHART)
    target_sources(lottiepp PRIVATE src/chart.cpp)
    target_compile_definitions(lottiepp PRIVATE ENABLE_CHART=1)
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
install(FILES src/lottiepp.hpp src/chart.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

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


