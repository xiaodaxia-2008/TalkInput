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
    ${PROJECT_SOURCE_DIR}/src/utils.cpp
)

add_executable(TalkInputOcrRecognitionTest
    ocr_recognition_test.cpp
)

add_executable(TalkInputRapidOcrTest
    rapid_ocr_test.cpp
)

target_link_libraries(TalkInputRapidOcrTest
    PRIVATE
    rapidocr-onnx-c-api
)
target_compile_definitions(TalkInputRapidOcrTest PRIVATE
    TALKINPUT_RAPID_OCR_MODEL_DIR="${TALKINPUT_RAPID_OCR_MODEL_DIR}"
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
)
target_include_directories(TalkInputRapidOcrTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)
if(WIN32)
    add_custom_command(TARGET TalkInputRapidOcrTest
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:TalkInputRapidOcrTest>
        $<TARGET_FILE_DIR:TalkInputRapidOcrTest>
        COMMAND_EXPAND_LISTS
    )
endif()

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
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
target_include_directories(TalkInputSenseVoiceTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
target_include_directories(TalkInputBenchmark PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
target_include_directories(TalkInputOcrCaptureTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
target_include_directories(TalkInputOcrRecognitionTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

add_executable(TalkInputOcrCompareTest
    ocr_compare_test.cpp
)
target_link_libraries(TalkInputOcrCompareTest
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Tesseract::libtesseract
    rapidocr-onnx-c-api
    windowsapp
    spdlog::spdlog_header_only
)
target_compile_definitions(TalkInputOcrCompareTest PRIVATE
    TALKINPUT_RAPID_OCR_MODEL_DIR="${TALKINPUT_RAPID_OCR_MODEL_DIR}"
    TALKINPUT_TESSERACT_TRAIN_DATA_DIR="${TALKINPUT_TESSERACT_TRAIN_DATA_DIR}"
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
)
target_include_directories(TalkInputOcrCompareTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
if(WIN32)
    add_custom_command(TARGET TalkInputOcrCompareTest
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:TalkInputOcrCompareTest>
        $<TARGET_FILE_DIR:TalkInputOcrCompareTest>
        COMMAND_EXPAND_LISTS
    )
endif()

# OpenVINO OCR test — optional, builds as stub if OpenVINO not found
find_package(OpenVINO QUIET)
find_package(Clipper2 CONFIG REQUIRED)
add_executable(TalkInputOpenVinoOcrTest
    openvino_ocr_test.cpp
)
if(OpenVINO_FOUND)
    target_compile_definitions(TalkInputOpenVinoOcrTest PRIVATE HAVE_OPENVINO)
    target_link_libraries(TalkInputOpenVinoOcrTest PRIVATE openvino::runtime)
    find_package(OpenCV QUIET)
    if(OpenCV_FOUND)
        target_link_libraries(TalkInputOpenVinoOcrTest PRIVATE opencv_core opencv_imgproc opencv_imgcodecs)
    endif()
endif()
# Python benchmark: tests/openvino_benchmark.py (run with .venv python)
#   uv pip install --python .venv/Scripts/python.exe openvino onnxruntime opencv-python
#   .venv/Scripts/python.exe tests/openvino_benchmark.py

add_executable(TalkInputPpOcrV6Test
    ppocrv6_ocr_test.cpp
)
target_link_libraries(TalkInputPpOcrV6Test PRIVATE Qt6::Core Qt6::Gui)
target_include_directories(TalkInputPpOcrV6Test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(TalkInputPpOcrV6Test PRIVATE
    TALKINPUT_PPOCRV6_MODEL_DIR="${PROJECT_SOURCE_DIR}/models/ppocrv6_small"
)
if(TALKINPUT_PPOCRV6_ORT_TARGET)
    target_compile_definitions(TalkInputPpOcrV6Test PRIVATE HAVE_ONNXRUNTIME)
    target_link_libraries(TalkInputPpOcrV6Test PRIVATE
        ${TALKINPUT_PPOCRV6_ORT_TARGET})
    if(WIN32)
        add_custom_command(TARGET TalkInputPpOcrV6Test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:TalkInputPpOcrV6Test>
            $<TARGET_FILE_DIR:TalkInputPpOcrV6Test>
            COMMAND_EXPAND_LISTS
        )
    endif()
endif()
if(TALKINPUT_PPOCRV6_OPENCV_TARGETS)
    target_compile_definitions(TalkInputPpOcrV6Test PRIVATE HAVE_OPENCV)
    target_link_libraries(TalkInputPpOcrV6Test PRIVATE
        ${TALKINPUT_PPOCRV6_OPENCV_TARGETS}
        Clipper2::Clipper2)
    if(WIN32)
        add_custom_command(TARGET TalkInputPpOcrV6Test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${TALKINPUT_OPENCV_WORLD_DLL}"
            $<TARGET_FILE_DIR:TalkInputPpOcrV6Test>)
    endif()
endif()
if(OpenVINO_FOUND)
    target_compile_definitions(TalkInputPpOcrV6Test PRIVATE HAVE_OPENVINO)
    target_link_libraries(TalkInputPpOcrV6Test PRIVATE openvino::runtime)
endif()

add_executable(TalkInputWinRTSpeechTest
    winrt_speech_test.cpp
)

target_link_libraries(TalkInputWinRTSpeechTest
    PRIVATE
    windowsapp
)

add_executable(TalkInputSegmentTest
    segment_test.cpp
    ${PROJECT_SOURCE_DIR}/src/audio_utils.cpp
)

add_executable(TalkInputAudioUtilsTest
    audio_utils_test.cpp
    ${PROJECT_SOURCE_DIR}/src/audio_utils.cpp
)
target_link_libraries(TalkInputSegmentTest
    PRIVATE
    Qt6::Core
    Qt6::Multimedia
    spdlog::spdlog_header_only
)
target_compile_definitions(TalkInputSegmentTest PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
target_include_directories(TalkInputSegmentTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)

target_link_libraries(TalkInputAudioUtilsTest
    PRIVATE
    Qt6::Core
    Qt6::Multimedia
    spdlog::spdlog_header_only
)
target_compile_definitions(TalkInputAudioUtilsTest PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
target_include_directories(TalkInputAudioUtilsTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)
