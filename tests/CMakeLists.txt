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

add_executable(test_host_identity
    test_host_identity.cpp
)

target_include_directories(test_host_identity
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_host_identity
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_host_identity COMMAND test_host_identity)

add_executable(test_aggregate
    test_aggregate.cpp
    ../src/report/ReportCli.cpp
    ../src/daemon/change_explainer.cpp
    ../src/daemon/counterfactual.cpp
)

target_include_directories(test_aggregate
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_aggregate
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_aggregate COMMAND test_aggregate)

add_executable(test_fleet_model
    test_fleet_model.cpp
    ../src/ui/backend/FleetModel.cpp
)

target_include_directories(test_fleet_model
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_fleet_model
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_fleet_model COMMAND test_fleet_model)

add_executable(test_host_identity
    test_host_identity.cpp
)

target_include_directories(test_host_identity
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_host_identity
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_host_identity COMMAND test_host_identity)

add_executable(test_aggregate
    test_aggregate.cpp
)

target_include_directories(test_aggregate
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_aggregate
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_aggregate COMMAND test_aggregate)

add_executable(test_fleet_model
    test_fleet_model.cpp
    ../src/ui/backend/FleetModel.cpp
)

target_include_directories(test_fleet_model
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_fleet_model
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_fleet_model COMMAND test_fleet_model)
