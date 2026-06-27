add_executable(TalkInputAsrTest
    asr_test.cpp
)

add_executable(TalkInputSenseVoiceTest
    sense_voice_test.cpp
)

add_executable(TalkInputBenchmark
    benchmark.cpp
)

add_executable(TalkInputOcrCaptureTest
    ocr_capture_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils.cpp
)

add_executable(TalkInputOcrRecognitionTest
    ocr_recognition_test.cpp
)

target_link_libraries(TalkInputAsrTest
    PRIVATE
    Qt6::Core
    sherpa-onnx-c-api
    spdlog::spdlog_header_only
)
target_compile_definitions(TalkInputAsrTest PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_link_libraries(TalkInputSenseVoiceTest
    PRIVATE
    Qt6::Core
    sherpa-onnx-c-api
    spdlog::spdlog_header_only
)
target_compile_definitions(TalkInputSenseVoiceTest PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_link_libraries(TalkInputBenchmark
    PRIVATE
    Qt6::Core
    sherpa-onnx-c-api
    spdlog::spdlog_header_only
)
target_compile_definitions(TalkInputBenchmark PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_link_libraries(TalkInputOcrCaptureTest
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    spdlog::spdlog_header_only
)
target_compile_definitions(TalkInputOcrCaptureTest PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_link_libraries(TalkInputOcrRecognitionTest
    PRIVATE
    windowsapp
)
target_compile_definitions(TalkInputOcrRecognitionTest PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_include_directories(TalkInputAsrTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
target_include_directories(TalkInputSenseVoiceTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
target_include_directories(TalkInputBenchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
target_include_directories(TalkInputOcrCaptureTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
target_include_directories(TalkInputOcrRecognitionTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_executable(TalkInputWinRTSpeechTest
    winrt_speech_test.cpp
)

target_link_libraries(TalkInputWinRTSpeechTest
    PRIVATE
    windowsapp
)

add_executable(TalkInputSegmentTest
    segment_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../audio_utils.cpp
)
target_link_libraries(TalkInputSegmentTest
    PRIVATE
    Qt6::Core
    Qt6::Multimedia
    spdlog::spdlog_header_only
)
target_compile_definitions(TalkInputSegmentTest PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
target_include_directories(TalkInputSegmentTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
