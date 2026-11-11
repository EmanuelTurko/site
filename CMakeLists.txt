cmake_minimum_required(VERSION 3.20)
project (site CXX)

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED on)

find_package(Boost 1.81 REQUIRED CONFIG COMPONENTS headers)
find_package(Threads REQUIRED)

add_executable(site
        src/main.cpp
        src/boostJson.cpp
        src/http/router.cpp
        src/http/server.cpp
        src/http/staticFiles.cpp
        src/config.cpp
        src/site/mustache.cpp
        src/page.cpp
        )

target_include_directories(site PRIVATE src)
target_link_libraries(site PRIVATE Boost::headers Threads::Threads)
target_compile_options(site PRIVATE -Wall -Wextra)
target_compile_definitions(site PRIVATE
        SITE_DEFAULT_ROOT="${CMAKE_SOURCE_DIR}"
        BOOST_ASIO_NO_DEPRECATED)

