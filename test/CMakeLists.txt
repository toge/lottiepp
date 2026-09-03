add_executable(smoke_wasi_minimal smoke_wasi_minimal.cpp)
target_link_libraries(smoke_wasi_minimal PRIVATE lottiepp)
target_compile_features(smoke_wasi_minimal PRIVATE ${STD_CPP})
target_compile_definitions(smoke_wasi_minimal PRIVATE LOTTIEPP_WASI_MINIMAL=1)
target_compile_options(smoke_wasi_minimal PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-exceptions>)
add_test(NAME smoke_wasi_minimal COMMAND smoke_wasi_minimal)

if(ENABLE_WASI_MINIMAL)
  return()
endif()

find_package(Catch2 REQUIRED CONFIG)

file(GLOB test_src test_*.cpp)
# smoke_wasi_minimal は既に登録済みなので除外
list(REMOVE_ITEM test_src ${CMAKE_CURRENT_SOURCE_DIR}/smoke_wasi_minimal.cpp)

add_executable(all_test ${test_src})

target_link_libraries(all_test PRIVATE Catch2::Catch2 Catch2::Catch2WithMain lottiepp)
target_include_directories(all_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_compile_features(all_test PRIVATE ${STD_CPP})

if(ENABLE_CHART)
    target_compile_definitions(all_test PRIVATE ENABLE_CHART=1)
endif()

add_test(NAME all_test COMMAND all_test -r junit)
