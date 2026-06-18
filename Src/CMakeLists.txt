qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES zh)

add_executable(TalkInput
    ${PROJECT_SOURCE_DIR}/resources/app.rc
    main.cpp
    main_window.cpp
    main_window.h
    main_window.ui
    voice_input_controller.cpp
    voice_input_controller.h
    asr_service.cpp
    asr_service.h
    app_config.cpp
    app_config.h
    json_utils.h
    archive_utils.cpp
    archive_utils.h
    asr_setting_widget.cpp
    asr_setting_widget.h
    history_widget.cpp
    history_widget.h
    llm_post_processor.cpp
    llm_post_processor.h
    ocr_service.cpp
    ocr_service.h
    ocr_service_windows.cpp
    ocr_service_windows.h
    logging.h
    recognition_history.cpp
    recognition_history.h
    model_registry.h
    model_registry.cpp
    recognizers/speech_recognizer.cpp
    recognizers/speech_recognizer.h
    recognizers/online_speech_recognizer.cpp
    recognizers/online_speech_recognizer.h
    recognizers/offline_speech_recognizer.cpp
    recognizers/offline_speech_recognizer.h
    recognizers/streaming_transducer_recognizer.cpp
    recognizers/streaming_transducer_recognizer.h
    recognizers/streaming_paraformer_recognizer.cpp
    recognizers/streaming_paraformer_recognizer.h
    recognizers/sense_voice_recognizer.cpp
    recognizers/sense_voice_recognizer.h
    recognizers/funasr_nano_recognizer.cpp
    recognizers/funasr_nano_recognizer.h
    recognizers/qwen3_asr_recognizer.cpp
    recognizers/qwen3_asr_recognizer.h
    paste_text.h
    paste_text.cpp
    scroll_text_display.h
    scroll_text_display.cpp
    utils.h
    utils.cpp
)

qt_add_resources(TalkInput resources
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES
    "${PROJECT_SOURCE_DIR}/resources/app.qss"
    "${PROJECT_SOURCE_DIR}/resources/icon.png"
    "${PROJECT_SOURCE_DIR}/resources/icon.svg"
    "${PROJECT_SOURCE_DIR}/resources/mic.svg"
    "${PROJECT_SOURCE_DIR}/resources/stop.svg"
    "${PROJECT_SOURCE_DIR}/resources/folder.svg"
    "${PROJECT_SOURCE_DIR}/resources/folder-plus.svg"
    "${PROJECT_SOURCE_DIR}/resources/check.svg"
    "${PROJECT_SOURCE_DIR}/resources/download.svg"
    "${PROJECT_SOURCE_DIR}/resources/globe.svg"
    "${PROJECT_SOURCE_DIR}/resources/hotwords.svg"
    "${PROJECT_SOURCE_DIR}/resources/delete.svg"
    "${PROJECT_SOURCE_DIR}/resources/copy.svg"
    "${PROJECT_SOURCE_DIR}/resources/edit.svg"
    "${PROJECT_SOURCE_DIR}/resources/zh.svg"
    "${PROJECT_SOURCE_DIR}/resources/en.svg"
    "${PROJECT_SOURCE_DIR}/resources/config.json"
)

qt_add_translations(TalkInput)

target_link_libraries(TalkInput
    PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Multimedia
    Qt6::Network
    Qt6::Svg
    Qt6::Sql
    LibArchive::LibArchive
    sherpa-onnx-c-api
    QHotkey::QHotkey
    spdlog::spdlog_header_only
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(TalkInput PRIVATE windowsapp)
endif()

add_executable(TalkInputAsrTest
    asr_test.cpp
)

add_executable(TalkInputSenseVoiceTest
    sense_voice_test.cpp
)

add_executable(TalkInputBenchmark
    benchmark.cpp
)

target_link_libraries(TalkInputAsrTest
    PRIVATE
    Qt6::Core
    sherpa-onnx-c-api
    spdlog::spdlog_header_only
)

target_link_libraries(TalkInputSenseVoiceTest
    PRIVATE
    Qt6::Core
    sherpa-onnx-c-api
    spdlog::spdlog_header_only
)

target_link_libraries(TalkInputBenchmark
    PRIVATE
    Qt6::Core
    sherpa-onnx-c-api
    spdlog::spdlog_header_only
)

target_include_directories(TalkInput PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/ThirdParties/KDToolBox
)
target_include_directories(TalkInputAsrTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/ThirdParties/KDToolBox
)
target_include_directories(TalkInputSenseVoiceTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/ThirdParties/KDToolBox
)
target_include_directories(TalkInputBenchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/ThirdParties/KDToolBox
)

target_compile_definitions(TalkInput PRIVATE
    PROJECT_VERSION_STR="${PROJECT_VERSION}"
)

qt_deploy(TARGET TalkInput COMPONENT Runtime)

install(TARGETS TalkInput
    COMPONENT Runtime
)

install(TARGETS TalkInputAsrTest
    COMPONENT Runtime
)

install(TARGETS TalkInputSenseVoiceTest
    COMPONENT Runtime
)

if(WIN32)
    add_custom_command(TARGET TalkInput
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:TalkInput>
        $<TARGET_FILE_DIR:TalkInput>
        COMMAND_EXPAND_LISTS
    )
endif()
