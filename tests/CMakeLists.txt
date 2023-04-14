Include(FetchContent)

FetchContent_Declare(Catch2
    SOURCE_DIR ${PROJECT_SOURCE_DIR}/third-party/catch2
)

FetchContent_MakeAvailable(Catch2)

add_executable(tests
    test_node.cpp
    test_node_view.cpp
    test_path_view.cpp
    test_volume.cpp
)

target_link_libraries(tests
    PRIVATE
    datastore
    Catch2::Catch2WithMain
)
