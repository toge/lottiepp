# smoke test: 常にビルド (hosted でも wasi でも)
add_executable(smoke smoke.cpp)
target_link_libraries(smoke PRIVATE lottiepp)
target_compile_features(smoke PRIVATE ${STD_CPP})

# wasm バイナリは直接実行できないため、wasmtime 経由で実行する
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  find_program(WASMTIME_EXECUTABLE wasmtime)
  if(WASMTIME_EXECUTABLE)
    add_test(NAME smoke COMMAND ${WASMTIME_EXECUTABLE} run smoke)
  else()
    message(STATUS "wasmtime not found: smoke test will not run under ctest (run manually: wasmtime run ./smoke)")
  endif()
else()
  add_test(NAME smoke COMMAND smoke)
endif()

file(GLOB test_src test_*.cpp)

# Catch2 は wasm32-wasip1/wasip2 ではビルドできないため find_package 自体をスキップする
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "wasm32")
  return()
endif()

find_package(Catch2 CONFIG REQUIRED)

# smoke.cpp は既に登録済みなので除外
list(REMOVE_ITEM test_src ${CMAKE_CURRENT_SOURCE_DIR}/smoke.cpp)

add_executable(all_test ${test_src})

target_link_libraries(all_test PRIVATE Catch2::Catch2 Catch2::Catch2WithMain lottiepp)
target_include_directories(all_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_compile_features(all_test PRIVATE ${STD_CPP})

if(ENABLE_CHART)
  target_compile_definitions(all_test PRIVATE ENABLE_CHART=1)
  target_link_libraries(all_test PRIVATE lottiepp_chart)
endif()

add_test(NAME all_test COMMAND all_test)
