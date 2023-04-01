cmake_minimum_required(VERSION 3.24)

project(datastore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(datastore
    include/datastore/mount_point.hpp
    include/datastore/node.hpp
    include/datastore/vault.hpp
    include/datastore/volume.hpp

    src/volume.cpp
)

target_include_directories(datastore PUBLIC ${PROJECT_SOURCE_DIR}/include)

add_subdirectory(tests)
