# Examples — standalone demos and benchmarks (not unit tests)

add_executable(ZennyAsrExample
    asr_example.cpp
)
add_executable(ZennySenseVoiceExample
    sense_voice_example.cpp
)
add_executable(ZennyAsrBenchmark
    asr_benchmark.cpp
)
add_executable(ZennyFunAsrParallelBenchmark
    funasr_parallel_benchmark.cpp
)
add_executable(ZennyOcrCaptureExample
    ocr_capture_example.cpp
    ${PROJECT_SOURCE_DIR}/src/utils.cpp
)
add_executable(ZennyOcrWinRtExample
    ocr_winrt_example.cpp
)
add_executable(ZennyPpOcrV6Example
    ppocrv6_ocr_example.cpp
)
add_executable(ZennyWinRtSpeechExample
    winrt_speech_example.cpp
)
add_executable(ZennyAudioSegmentExample
    audio_segment_example.cpp
    ${PROJECT_SOURCE_DIR}/src/audio_utils.cpp
)
add_executable(ZennyLongAudioExample
    long_audio_example.cpp
    ${PROJECT_SOURCE_DIR}/src/audio_utils.cpp
    ${PROJECT_SOURCE_DIR}/src/app_config.cpp
    ${PROJECT_SOURCE_DIR}/src/json_utils.cpp
    ${PROJECT_SOURCE_DIR}/src/speech_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/src/utils.cpp
    ${PROJECT_SOURCE_DIR}/src/recognizers/offline_speech_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/src/recognizers/online_speech_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/src/recognizers/sense_voice_speech_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/src/recognizers/funasr_nano_speech_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/src/recognizers/streaming_paraformer_speech_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/resources/misc/config.json
)
qt_add_resources(ZennyLongAudioExample "long_audio_resources"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES
    "${PROJECT_SOURCE_DIR}/resources/misc/config.json"
)

add_executable(ZennyLongAudioBenchmark
    long_audio_benchmark.cpp
    ${PROJECT_SOURCE_DIR}/src/audio_utils.cpp
    ${PROJECT_SOURCE_DIR}/src/app_config.cpp
    ${PROJECT_SOURCE_DIR}/src/json_utils.cpp
    ${PROJECT_SOURCE_DIR}/src/speech_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/src/utils.cpp
    ${PROJECT_SOURCE_DIR}/src/recognizers/offline_speech_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/src/recognizers/online_speech_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/src/recognizers/sense_voice_speech_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/src/recognizers/funasr_nano_speech_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/src/recognizers/streaming_paraformer_speech_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/resources/misc/config.json
)
qt_add_resources(ZennyLongAudioBenchmark "long_audio_benchmark_resources"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES
    "${PROJECT_SOURCE_DIR}/resources/misc/config.json"
)

add_executable(ZennyMeetingDiarizationExample
    meeting_diarization_example.cpp
    ${PROJECT_SOURCE_DIR}/src/audio_utils.cpp
    ${PROJECT_SOURCE_DIR}/src/app_config.cpp
    ${PROJECT_SOURCE_DIR}/src/json_utils.cpp
    ${PROJECT_SOURCE_DIR}/src/speaker_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/src/speech_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/src/utils.cpp
    ${PROJECT_SOURCE_DIR}/src/recognizers/offline_speech_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/src/recognizers/online_speech_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/src/recognizers/sense_voice_speech_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/src/recognizers/funasr_nano_speech_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/src/recognizers/streaming_paraformer_speech_recognizer.cpp
    ${PROJECT_SOURCE_DIR}/resources/misc/config.json
)
qt_add_resources(ZennyMeetingDiarizationExample "meeting_diarization_resources"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES
    "${PROJECT_SOURCE_DIR}/resources/misc/config.json"
)

target_link_libraries(ZennyAsrExample
    PRIVATE
    Qt6::Core
    sherpa-onnx-c-api
    spdlog::spdlog_header_only
)
target_compile_definitions(ZennyAsrExample PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_link_libraries(ZennySenseVoiceExample
    PRIVATE
    Qt6::Core
    sherpa-onnx-c-api
    spdlog::spdlog_header_only
)
target_compile_definitions(ZennySenseVoiceExample PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_link_libraries(ZennyAsrBenchmark
    PRIVATE
    Qt6::Core
    sherpa-onnx-c-api
    spdlog::spdlog_header_only
)
target_compile_definitions(ZennyAsrBenchmark PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_link_libraries(ZennyFunAsrParallelBenchmark
    PRIVATE
    Qt6::Core
    sherpa-onnx-c-api
    spdlog::spdlog_header_only
)
target_compile_definitions(ZennyFunAsrParallelBenchmark PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
)

target_link_libraries(ZennyOcrCaptureExample
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    spdlog::spdlog_header_only
)
target_compile_definitions(ZennyOcrCaptureExample PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_link_libraries(ZennyOcrWinRtExample
    PRIVATE
    windowsapp
)
target_compile_definitions(ZennyOcrWinRtExample PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

find_package(Clipper2 CONFIG REQUIRED)

target_link_libraries(ZennyPpOcrV6Example PRIVATE Qt6::Core Qt6::Gui)
target_include_directories(ZennyPpOcrV6Example PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(ZennyPpOcrV6Example PRIVATE
    ZENNY_PPOCRV6_MODEL_DIR="${PROJECT_SOURCE_DIR}/models/ppocrv6_small"
)
if(ZENNY_PPOCRV6_ORT_TARGET)
    target_compile_definitions(ZennyPpOcrV6Example PRIVATE HAVE_ONNXRUNTIME)
    target_link_libraries(ZennyPpOcrV6Example PRIVATE
        ${ZENNY_PPOCRV6_ORT_TARGET})
    if(WIN32)
        add_custom_command(TARGET ZennyPpOcrV6Example POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:ZennyPpOcrV6Example>
            $<TARGET_FILE_DIR:ZennyPpOcrV6Example>
            COMMAND_EXPAND_LISTS
        )
    endif()
endif()
if(ZENNY_PPOCRV6_OPENCV_TARGETS)
    target_compile_definitions(ZennyPpOcrV6Example PRIVATE HAVE_OPENCV)
    target_link_libraries(ZennyPpOcrV6Example PRIVATE
        ${ZENNY_PPOCRV6_OPENCV_TARGETS}
        Clipper2::Clipper2)
    if(WIN32)
        add_custom_command(TARGET ZennyPpOcrV6Example POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ZENNY_OPENCV_WORLD_DLL}"
            $<TARGET_FILE_DIR:ZennyPpOcrV6Example>)
    endif()
endif()

target_link_libraries(ZennyWinRtSpeechExample
    PRIVATE
    windowsapp
)

target_link_libraries(ZennyAudioSegmentExample
    PRIVATE
    Qt6::Core
    Qt6::Multimedia
    spdlog::spdlog_header_only
)
target_compile_definitions(ZennyAudioSegmentExample PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
target_include_directories(ZennyAudioSegmentExample PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)

target_link_libraries(ZennyLongAudioExample
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    sherpa-onnx-c-api
    nlohmann_json::nlohmann_json
    spdlog::spdlog_header_only
)
target_compile_definitions(ZennyLongAudioExample PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
target_include_directories(ZennyLongAudioExample PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)

target_link_libraries(ZennyLongAudioBenchmark
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    sherpa-onnx-c-api
    nlohmann_json::nlohmann_json
    spdlog::spdlog_header_only
)
target_compile_definitions(ZennyLongAudioBenchmark PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
target_include_directories(ZennyLongAudioBenchmark PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)

target_link_libraries(ZennyMeetingDiarizationExample
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    sherpa-onnx-c-api
    nlohmann_json::nlohmann_json
    spdlog::spdlog_header_only
)
target_compile_definitions(ZennyMeetingDiarizationExample PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
target_include_directories(ZennyMeetingDiarizationExample PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)

# Include dirs for examples that need them
target_include_directories(ZennyAsrExample PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
target_include_directories(ZennySenseVoiceExample PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
target_include_directories(ZennyAsrBenchmark PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
target_include_directories(ZennyOcrCaptureExample PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
target_include_directories(ZennyOcrWinRtExample PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)
