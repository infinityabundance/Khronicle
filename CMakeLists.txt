cmake_minimum_required(VERSION 3.21)

project(Khronicle LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Kirigami2)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(SQLite3 REQUIRED)

add_executable(khronicle-daemon
    src/daemon/main.cpp
    src/daemon/khronicle_store.cpp
)

target_include_directories(khronicle-daemon
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(khronicle-daemon
    PRIVATE
        Qt6::Core
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
)

add_executable(khronicle
    src/ui/main.cpp
)

target_include_directories(khronicle
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
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
