cmake_minimum_required(VERSION 3.20)
project (site CXX)

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED on)

find_package(Boost 1.81 REQUIRED CONFIG COMPONENTS headers)
find_package(Threads REQUIRED)

# add all code except main.cpp
add_library(sitecore
        src/boostJson.cpp
        src/http/router.cpp
        src/http/server.cpp
        src/http/staticFiles.cpp
        src/config.cpp
        src/site/mustache.cpp
        src/page.cpp
)
target_include_directories(sitecore PUBLIC src)
target_link_libraries(sitecore PUBLIC Boost::headers Threads::Threads)
target_compile_options(sitecore PUBLIC -Wall -Wextra)
target_compile_definitions(sitecore PUBLIC
        SITE_DEFAULT_ROOT="${CMAKE_SOURCE_DIR}"
        BOOST_ASIO_NO_DEPRECATED)

# server is main.cpp + library
add_executable(site src/main.cpp)
target_link_libraries(site PRIVATE sitecore)


# tests
include(FetchContent)
FetchContent_Declare(googletest
                    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(googletest) # download + make GTest:: targets exist

enable_testing() # turn on ctest for project

add_executable(site_tests tests/mustacheTest.cpp)
target_link_libraries(site_tests PRIVATE sitecore GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(site_tests)

