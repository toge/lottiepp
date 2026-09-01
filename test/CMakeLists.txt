find_package(Catch2 REQUIRED CONFIG)

file(GLOB test_src test_*.cpp)

add_executable(all_test ${test_src})

target_link_libraries(all_test PRIVATE Catch2::Catch2 Catch2::Catch2WithMain lottiepp)
target_include_directories(all_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_compile_features(all_test PRIVATE ${STD_CPP})

# chart ヘルパのテストは ENABLE_CHART=ON のときのみ有効化する
if(ENABLE_CHART)
    target_compile_definitions(all_test PRIVATE ENABLE_CHART=1)
endif()

add_test(NAME all_test COMMAND all_test -r junit)
