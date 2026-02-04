enable_testing()

# Enable Qt MOC for tests
set(CMAKE_AUTOMOC ON)

add_executable(test_models_and_json
    test_models_and_json.cpp
)

target_include_directories(test_models_and_json
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_models_and_json
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_models_and_json COMMAND test_models_and_json)

add_executable(test_logging
    test_logging.cpp
    ../src/common/logging.cpp
)

target_include_directories(test_logging
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_logging
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_logging COMMAND test_logging)

add_executable(test_replay
    test_replay.cpp
    ../src/replay/ReplayHarness.cpp
    ../src/report/ReportCli.cpp
    ../src/daemon/khronicle_store.cpp
    ../src/daemon/khronicle_api_server.cpp
    ../src/daemon/khronicle_daemon.cpp
    ../src/daemon/change_explainer.cpp
    ../src/daemon/counterfactual.cpp
    ../src/daemon/pacman_parser.cpp
    ../src/daemon/journal_parser.cpp
    ../src/daemon/snapshot_builder.cpp
    ../src/daemon/watch_engine.cpp
    ../src/common/logging.cpp
    ../src/common/process_utils.cpp
    ../src/debug/scenario_capture.cpp
)

target_include_directories(test_replay
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_replay
    PRIVATE
        Qt6::Core
        Qt6::Test
        Qt6::Network
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_replay COMMAND test_replay)

add_executable(test_store
    test_store.cpp
    ../src/daemon/khronicle_store.cpp
    ../src/common/logging.cpp
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

add_executable(test_pacman_parser
    test_pacman_parser.cpp
    ../src/daemon/pacman_parser.cpp
    ../src/common/logging.cpp
)

target_include_directories(test_pacman_parser
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_pacman_parser
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_pacman_parser COMMAND test_pacman_parser)

add_executable(test_journal_parser
    test_journal_parser.cpp
    ../src/daemon/journal_parser.cpp
    ../src/common/logging.cpp
)

target_include_directories(test_journal_parser
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_journal_parser
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_journal_parser COMMAND test_journal_parser)

add_executable(test_risk_classifier
    test_risk_classifier.cpp
    ../src/daemon/watch_engine.cpp
    ../src/daemon/khronicle_store.cpp
    ../src/common/logging.cpp
)

target_include_directories(test_risk_classifier
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_risk_classifier
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_risk_classifier COMMAND test_risk_classifier)

add_executable(test_watch_engine
    test_watch_engine.cpp
    ../src/daemon/watch_engine.cpp
    ../src/daemon/khronicle_store.cpp
    ../src/common/logging.cpp
)

target_include_directories(test_watch_engine
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_watch_engine
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_watch_engine COMMAND test_watch_engine)

add_executable(test_temporal_reasoning
    test_temporal_reasoning.cpp
    ../src/daemon/change_explainer.cpp
    ../src/daemon/counterfactual.cpp
    ../src/common/logging.cpp
)

target_include_directories(test_temporal_reasoning
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_temporal_reasoning
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_temporal_reasoning COMMAND test_temporal_reasoning)

add_executable(test_api_server
    test_api_server.cpp
    ../src/daemon/khronicle_api_server.cpp
    ../src/daemon/khronicle_store.cpp
    ../src/daemon/change_explainer.cpp
    ../src/daemon/counterfactual.cpp
    ../src/common/logging.cpp
    ../src/debug/scenario_capture.cpp
)

target_include_directories(test_api_server
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_api_server
    PRIVATE
        Qt6::Core
        Qt6::Test
        Qt6::Network
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_api_server COMMAND test_api_server)

add_executable(test_report_cli
    test_report_cli.cpp
    ../src/report/ReportCli.cpp
    ../src/daemon/change_explainer.cpp
    ../src/daemon/counterfactual.cpp
    ../src/daemon/khronicle_store.cpp
    ../src/common/logging.cpp
    ../src/debug/scenario_capture.cpp
)

target_include_directories(test_report_cli
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_report_cli
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_report_cli COMMAND test_report_cli)

add_executable(test_temporal
    test_temporal.cpp
    ../src/daemon/khronicle_store.cpp
    ../src/common/logging.cpp
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
    ../src/common/logging.cpp
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
    ../src/daemon/khronicle_store.cpp
    ../src/common/logging.cpp
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
    ../src/daemon/khronicle_store.cpp
    ../src/common/logging.cpp
    ../src/debug/scenario_capture.cpp
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

add_executable(test_snapshot_builder
    test_snapshot_builder.cpp
    ../src/daemon/snapshot_builder.cpp
    ../src/common/logging.cpp
)

target_include_directories(test_snapshot_builder
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
)

target_link_libraries(test_snapshot_builder
    PRIVATE
        Qt6::Core
        Qt6::Test
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_test(NAME test_snapshot_builder COMMAND test_snapshot_builder)
