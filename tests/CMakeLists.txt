Include(FetchContent)

FetchContent_Declare(Catch2
    SOURCE_DIR ${PROJECT_SOURCE_DIR}/third-party/catch2
)

FetchContent_MakeAvailable(Catch2)

add_executable(unit_tests
    test_node.cpp
    test_node_view.cpp
    test_path_view.cpp
    test_volume.cpp
)

target_link_libraries(unit_tests
    PRIVATE
    datastore
    Catch2::Catch2WithMain
)

add_executable(load_tests
    load_test.cpp
)

target_link_libraries(load_tests
    PRIVATE
    datastore
    Catch2::Catch2WithMain
)
