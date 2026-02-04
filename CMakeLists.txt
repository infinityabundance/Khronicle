cmake_minimum_required(VERSION 3.21)

project(Khronicle LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Kirigami2 Test)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(SQLite3 REQUIRED)

add_executable(khronicle-daemon
    src/daemon/main.cpp
    src/daemon/khronicle_store.cpp
    src/daemon/pacman_parser.cpp
    src/daemon/journal_parser.cpp
    src/daemon/snapshot_builder.cpp
    src/daemon/khronicle_api_server.cpp
    src/daemon/change_explainer.cpp
    src/daemon/counterfactual.cpp
    src/daemon/khronicle_daemon.cpp
    src/daemon/watch_engine.cpp
    src/common/logging.cpp
    src/debug/scenario_capture.cpp
)

target_include_directories(khronicle-daemon
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

target_link_libraries(khronicle-daemon
    PRIVATE
        Qt6::Core
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_executable(khronicle
    src/ui/main.cpp
    src/ui/backend/KhronicleApiClient.cpp
    src/ui/backend/FleetModel.cpp
    src/ui/backend/WatchClient.cpp
    src/common/logging.cpp
    src/debug/scenario_capture.cpp
)

target_include_directories(khronicle
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

target_compile_definitions(khronicle
    PRIVATE
        KHRONICLE_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/ui/qml"
)

target_link_libraries(khronicle
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Kirigami2
        nlohmann_json::nlohmann_json
)

add_executable(khronicle-tray
    src/tray/main.cpp
    src/tray/KhronicleTray.cpp
    src/common/logging.cpp
    src/debug/scenario_capture.cpp
)

target_include_directories(khronicle-tray
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

target_link_libraries(khronicle-tray
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

enable_testing()
add_subdirectory(tests)

add_executable(khronicle-report
    src/report/main.cpp
    src/report/ReportCli.cpp
    src/daemon/change_explainer.cpp
    src/daemon/counterfactual.cpp
    src/common/logging.cpp
    src/debug/scenario_capture.cpp
)

add_executable(khronicle-replay
    src/replay/main.cpp
    src/replay/ReplayHarness.cpp
    src/common/logging.cpp
    src/debug/scenario_capture.cpp
    src/daemon/khronicle_store.cpp
    src/daemon/khronicle_api_server.cpp
    src/daemon/change_explainer.cpp
    src/daemon/counterfactual.cpp
)

target_include_directories(khronicle-report
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

target_link_libraries(khronicle-report
    PRIVATE
        Qt6::Core
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

target_include_directories(khronicle-replay
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

target_link_libraries(khronicle-replay
    PRIVATE
        Qt6::Core
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

install(TARGETS khronicle khronicle-daemon khronicle-tray khronicle-report
    RUNTIME DESTINATION bin
)

install(TARGETS khronicle-replay
    RUNTIME DESTINATION bin
)

install(FILES
    resources/desktop/khronicle.desktop
    resources/desktop/khronicle-tray.desktop
    DESTINATION share/applications
)

install(FILES
    resources/systemd/khronicle-daemon.service
    resources/systemd/khronicle-tray.service
    DESTINATION share/systemd/user
)
