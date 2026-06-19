qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES zh)

add_executable(TalkInput
    ${PROJECT_SOURCE_DIR}/resources/misc/app.rc
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
    app_language.cpp
    app_language.h
    json_utils.h
    archive_utils.cpp
    archive_utils.h
    asr_setting_widget.cpp
    asr_setting_widget.h
    asr_setting_widget.ui
    history_widget.cpp
    history_widget.h
    history_widget.ui
    llm_post_processor.cpp
    llm_post_processor.h
    spawn_llama_server.cpp
    spawn_llama_server.h
    ocr_service.cpp
    ocr_service.h
    system_ocr_service.h
    speech_recognizer.h
    speech_recognizer.cpp
    system_speech_recognizer.h
    logging.cpp
    logging.h
    recognition_history.cpp
    recognition_history.h
    recognizers/online_speech_recognizer.cpp
    recognizers/online_speech_recognizer.h
    recognizers/offline_speech_recognizer.cpp
    recognizers/offline_speech_recognizer.h
    recognizers/streaming_paraformer_speech_recognizer.cpp
    recognizers/streaming_paraformer_speech_recognizer.h
    recognizers/sense_voice_speech_recognizer.cpp
    recognizers/sense_voice_speech_recognizer.h
    recognizers/funasr_nano_speech_recognizer.cpp
    recognizers/funasr_nano_speech_recognizer.h
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
    "${PROJECT_SOURCE_DIR}/resources/misc/app.qss"
    "${PROJECT_SOURCE_DIR}/resources/icons/icon.png"
    "${PROJECT_SOURCE_DIR}/resources/icons/icon.svg"
    "${PROJECT_SOURCE_DIR}/resources/icons/mic.svg"
    "${PROJECT_SOURCE_DIR}/resources/icons/stop.svg"
    "${PROJECT_SOURCE_DIR}/resources/icons/folder.svg"
    "${PROJECT_SOURCE_DIR}/resources/icons/folder-plus.svg"
    "${PROJECT_SOURCE_DIR}/resources/icons/check.svg"
    "${PROJECT_SOURCE_DIR}/resources/icons/download.svg"
    "${PROJECT_SOURCE_DIR}/resources/icons/globe.svg"
    "${PROJECT_SOURCE_DIR}/resources/icons/hotwords.svg"
    "${PROJECT_SOURCE_DIR}/resources/icons/delete.svg"
    "${PROJECT_SOURCE_DIR}/resources/icons/copy.svg"
    "${PROJECT_SOURCE_DIR}/resources/icons/edit.svg"
    "${PROJECT_SOURCE_DIR}/resources/icons/zh.svg"
    "${PROJECT_SOURCE_DIR}/resources/icons/en.svg"
    "${PROJECT_SOURCE_DIR}/resources/misc/config.json"
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
    target_link_libraries(TalkInput PRIVATE windowsapp ole32 uiautomationcore)
    target_sources(TalkInput PRIVATE
        windows/system_ocr_service.cpp
        windows/system_speech_recognizer.cpp
    )
elseif(APPLE)
    target_sources(TalkInput PRIVATE
        macos/system_ocr_service.cpp
        macos/system_speech_recognizer.cpp
    )
elseif(UNIX)
    target_sources(TalkInput PRIVATE
        linux/system_ocr_service.cpp
        linux/system_speech_recognizer.cpp
    )
endif()

target_include_directories(TalkInput PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)

target_compile_definitions(TalkInput PRIVATE
    PROJECT_VERSION_STR="${PROJECT_VERSION}"
    GIT_COMMIT_ID="${GIT_COMMIT_ID}"
    GIT_COMMIT_DATE="${GIT_COMMIT_DATE}"
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
)

qt_deploy(TARGET TalkInput COMPONENT Runtime)

install(TARGETS TalkInput
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

add_subdirectory(tests)
