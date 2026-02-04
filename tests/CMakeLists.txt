add_executable(test_store
    test_store.cpp
)

target_include_directories(test_store
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_store
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_store COMMAND test_store)

add_executable(test_diff
    test_diff.cpp
)

target_include_directories(test_diff
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_diff
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_diff COMMAND test_diff)
