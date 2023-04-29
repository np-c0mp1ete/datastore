cmake_minimum_required(VERSION 3.19)

project(datastore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CMakeDependentOption)

cmake_dependent_option(DATASTORE_ADDRESS_SANITIZER "Enable address sanitizer" OFF "UNIX" OFF)
cmake_dependent_option(DATASTORE_UB_SANITIZER "Enable undefined behavior sanitizer" OFF "UNIX" OFF)
cmake_dependent_option(DATASTORE_THREAD_SANITIZER "Enable thread sanitizer" ON "UNIX;NOT DATASTORE_ADDRESS_SANITIZER" OFF)
cmake_dependent_option(DATASTORE_MEMORY_SANITIZER "Enable memory sanitizer" ON 
    "UNIX;NOT DATASTORE_ADDRESS_SANITIZER;NOT DATASTORE_THREAD_SANITIZER" OFF)

if(MSVC)
    # Use the highest warning level for Visual Studio.
    set(CMAKE_CXX_WARNING_LEVEL 4)
    if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
        string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    else()
        string(APPEND CMAKE_CXX_FLAGS " /W4")
    endif()

    # Disable exceptions
    string(REGEX REPLACE "/EH[a-z]+" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
    string(APPEND CMAKE_CXX_FLAGS " /EHsc")

    # Disable RTTI
    string(REGEX REPLACE "/GR" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
    string(APPEND CMAKE_CXX_FLAGS " /GR-")

    # Generate debug information
    string(APPEND CMAKE_CXX_FLAGS " /Zi")
    string(APPEND CMAKE_EXE_LINKER_FLAGS " /DEBUG")
else()
    # Use -Wall for clang and gcc.
    if(NOT CMAKE_CXX_FLAGS MATCHES "-Wall")
        string(APPEND CMAKE_CXX_FLAGS " -Wall")
    endif()

    # Use -Wextra for clang and gcc.
    if(NOT CMAKE_CXX_FLAGS MATCHES "-Wextra")
        string(APPEND CMAKE_CXX_FLAGS " -Wextra")
    endif()

    # Use -Werror for clang and gcc.
    if(NOT CMAKE_CXX_FLAGS MATCHES "-Werror")
        string(APPEND CMAKE_CXX_FLAGS " -Werror")
    endif()

    # Disable exceptions
    string(REGEX REPLACE "-fexceptions" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
    string(APPEND CMAKE_CXX_FLAGS " -fno-exceptions")

    # Disable RTTI
    string(REGEX REPLACE "-frtti" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
    string(APPEND CMAKE_CXX_FLAGS " -fno-rtti")

    # Generate debug information
    string(APPEND CMAKE_CXX_FLAGS " -g")

    if (UNIX)
        string(APPEND CMAKE_CXX_FLAGS " -pthread")
    endif()

    if (DATASTORE_ADDRESS_SANITIZER)
        string(APPEND CMAKE_CXX_FLAGS " -fsanitize=address -fno-omit-frame-pointer")
        string(APPEND CMAKE_EXE_LINKER_FLAGS " -fsanitize=address")
    endif()

    if (DATASTORE_UB_SANITIZER)
        string(APPEND CMAKE_CXX_FLAGS " -fsanitize=undefined")
        string(APPEND CMAKE_EXE_LINKER_FLAGS " -fsanitize=undefined")
    endif()

    if (DATASTORE_THREAD_SANITIZER)
        string(APPEND CMAKE_CXX_FLAGS " -fsanitize=thread")
        string(APPEND CMAKE_EXE_LINKER_FLAGS " -fsanitize=thread")
    endif()

    if (DATASTORE_MEMORY_SANITIZER)
        string(APPEND CMAKE_CXX_FLAGS " -fsanitize=memory -fno-omit-frame-pointer")
        string(APPEND CMAKE_EXE_LINKER_FLAGS " -fsanitize=memory")
    endif()
endif()

add_library(datastore
    include/datastore/node.hpp
    include/datastore/node_view.hpp
    include/datastore/path_view.hpp
    include/datastore/vault.hpp
    include/datastore/volume.hpp

    include/datastore/detail/sorted_list.hpp
    include/datastore/detail/striped_hashmap.hpp

    src/node.cpp
    src/node_view.cpp
    src/vault.cpp
    src/volume.cpp
)

target_include_directories(datastore PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(datastore PUBLIC DATASTORE_DEBUG=1)

add_subdirectory(examples)
add_subdirectory(tests)
