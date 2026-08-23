# Examples — standalone demos and benchmarks (not unit tests)

add_executable(TalkInputAsrExample
    asr_example.cpp
)
add_executable(TalkInputSenseVoiceExample
    sense_voice_example.cpp
)
add_executable(TalkInputAsrBenchmark
    asr_benchmark.cpp
)
add_executable(TalkInputOcrCaptureExample
    ocr_capture_example.cpp
    ${PROJECT_SOURCE_DIR}/src/utils.cpp
)
add_executable(TalkInputOcrWinRtExample
    ocr_winrt_example.cpp
)
add_executable(TalkInputOcrCompareBenchmark
    ocr_compare_benchmark.cpp
)
add_executable(TalkInputPpOcrV6Example
    ppocrv6_ocr_example.cpp
)
add_executable(TalkInputWinRtSpeechExample
    winrt_speech_example.cpp
)
add_executable(TalkInputAudioSegmentExample
    audio_segment_example.cpp
    ${PROJECT_SOURCE_DIR}/src/audio_utils.cpp
)

target_link_libraries(TalkInputAsrExample
    PRIVATE
    Qt6::Core
    sherpa-onnx-c-api
    spdlog::spdlog_header_only
)
target_compile_definitions(TalkInputAsrExample PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_link_libraries(TalkInputSenseVoiceExample
    PRIVATE
    Qt6::Core
    sherpa-onnx-c-api
    spdlog::spdlog_header_only
)
target_compile_definitions(TalkInputSenseVoiceExample PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_link_libraries(TalkInputAsrBenchmark
    PRIVATE
    Qt6::Core
    sherpa-onnx-c-api
    spdlog::spdlog_header_only
)
target_compile_definitions(TalkInputAsrBenchmark PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_link_libraries(TalkInputOcrCaptureExample
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    spdlog::spdlog_header_only
)
target_compile_definitions(TalkInputOcrCaptureExample PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_link_libraries(TalkInputOcrWinRtExample
    PRIVATE
    windowsapp
)
target_compile_definitions(TalkInputOcrWinRtExample PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_link_libraries(TalkInputOcrCompareBenchmark
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Tesseract::libtesseract
    windowsapp
    spdlog::spdlog_header_only
)
target_compile_definitions(TalkInputOcrCompareBenchmark PRIVATE
    TALKINPUT_TESSERACT_TRAIN_DATA_DIR="${TALKINPUT_TESSERACT_TRAIN_DATA_DIR}"
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
)
target_include_directories(TalkInputOcrCompareBenchmark PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
if(WIN32)
    add_custom_command(TARGET TalkInputOcrCompareBenchmark
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:TalkInputOcrCompareBenchmark>
        $<TARGET_FILE_DIR:TalkInputOcrCompareBenchmark>
        COMMAND_EXPAND_LISTS
    )
endif()

find_package(Clipper2 CONFIG REQUIRED)

target_link_libraries(TalkInputPpOcrV6Example PRIVATE Qt6::Core Qt6::Gui)
target_include_directories(TalkInputPpOcrV6Example PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(TalkInputPpOcrV6Example PRIVATE
    TALKINPUT_PPOCRV6_MODEL_DIR="${PROJECT_SOURCE_DIR}/models/ppocrv6_small"
)
if(TALKINPUT_PPOCRV6_ORT_TARGET)
    target_compile_definitions(TalkInputPpOcrV6Example PRIVATE HAVE_ONNXRUNTIME)
    target_link_libraries(TalkInputPpOcrV6Example PRIVATE
        ${TALKINPUT_PPOCRV6_ORT_TARGET})
    if(WIN32)
        add_custom_command(TARGET TalkInputPpOcrV6Example POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:TalkInputPpOcrV6Example>
            $<TARGET_FILE_DIR:TalkInputPpOcrV6Example>
            COMMAND_EXPAND_LISTS
        )
    endif()
endif()
if(TALKINPUT_PPOCRV6_OPENCV_TARGETS)
    target_compile_definitions(TalkInputPpOcrV6Example PRIVATE HAVE_OPENCV)
    target_link_libraries(TalkInputPpOcrV6Example PRIVATE
        ${TALKINPUT_PPOCRV6_OPENCV_TARGETS}
        Clipper2::Clipper2)
    if(WIN32)
        add_custom_command(TARGET TalkInputPpOcrV6Example POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${TALKINPUT_OPENCV_WORLD_DLL}"
            $<TARGET_FILE_DIR:TalkInputPpOcrV6Example>)
    endif()
endif()

target_link_libraries(TalkInputWinRtSpeechExample
    PRIVATE
    windowsapp
)

target_link_libraries(TalkInputAudioSegmentExample
    PRIVATE
    Qt6::Core
    Qt6::Multimedia
    spdlog::spdlog_header_only
)
target_compile_definitions(TalkInputAudioSegmentExample PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
target_include_directories(TalkInputAudioSegmentExample PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)

# Include dirs for examples that need them
target_include_directories(TalkInputAsrExample PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
target_include_directories(TalkInputSenseVoiceExample PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
target_include_directories(TalkInputAsrBenchmark PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
target_include_directories(TalkInputOcrCaptureExample PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
target_include_directories(TalkInputOcrWinRtExample PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)
