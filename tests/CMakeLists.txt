find_package(Catch2 3 QUIET)

include(CTest)
include(Catch)

add_executable(ZennyTests
    audio_utils_test.cpp
    ${PROJECT_SOURCE_DIR}/src/audio_utils.cpp
)

target_link_libraries(ZennyTests
    PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    Qt6::Multimedia
    spdlog::spdlog_header_only
)
target_compile_definitions(ZennyTests PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
target_include_directories(ZennyTests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)

catch_discover_tests(ZennyTests)
