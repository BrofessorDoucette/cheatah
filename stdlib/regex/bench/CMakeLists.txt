# Standalone benchmark project for `cheatah::regex` vs std::regex vs boost::regex.
# Boost is a BENCHMARK-ONLY dependency: it is fetched + built here (never by the normal
# cheatah build). Configure/build/run:
#     cmake -S stdlib/regex/bench -B build/regexbench -DCMAKE_BUILD_TYPE=Release
#     cmake --build build/regexbench -j
#     ./build/regexbench/rxbench
cmake_minimum_required(VERSION 3.24)
project(cheatah_regex_bench CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
# Only build Boost.Regex (and its transitive deps) from the CMake source release.
set(BOOST_INCLUDE_LIBRARIES regex)
set(BOOST_ENABLE_CMAKE ON)
FetchContent_Declare(Boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.85.0/boost-1.85.0-cmake.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(Boost)

add_executable(rxbench regex_bench.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../regex.cpp)
target_include_directories(rxbench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(rxbench PRIVATE CHEATAH_REGEX_BENCH_BOOST)
target_compile_options(rxbench PRIVATE -O3)
target_link_libraries(rxbench PRIVATE Boost::regex)
