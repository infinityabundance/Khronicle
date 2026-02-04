enable_testing()

add_executable(test_temporal
    test_temporal.cpp
)

target_include_directories(test_temporal
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_temporal
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_temporal COMMAND test_temporal)

add_executable(test_explainer
    test_explainer.cpp
    ../src/daemon/change_explainer.cpp
)

target_include_directories(test_explainer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_explainer
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_explainer COMMAND test_explainer)
