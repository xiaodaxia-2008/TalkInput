qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES zh)

add_executable(Zenny WIN32
    ${PROJECT_SOURCE_DIR}/resources/misc/app.rc
    ${PROJECT_SOURCE_DIR}/resources/resources.qrc
    main.cpp
    single_instance.cpp
    single_instance.h
    main_window.cpp
    main_window.h
    main_window.ui
    voice_hotkey.cpp
    voice_hotkey.h
    voice_pipeline_controller.cpp
    voice_pipeline_controller.h
    voice_overlay.cpp
    voice_overlay.h
    app_config.cpp
    app_config.h
    json_utils.cpp
    json_utils.h
    archive_utils.cpp
    archive_utils.h
    audio_utils.cpp
    audio_utils.h
    model_download.cpp
    model_download.h
    theme.cpp
    theme.h
    stt_settings_widget.cpp
    stt_settings_widget.h
    stt_settings_widget.ui
    ocr_settings_widget.cpp
    ocr_settings_widget.h
    ocr_settings_widget.ui
    llm_settings_widget.cpp
    llm_settings_widget.h
    llm_settings_widget.ui
    tts_settings_widget.cpp
    tts_settings_widget.h
    tts_settings_widget.ui
    api_server_settings_widget.cpp
    api_server_settings_widget.h
    api_server_settings_widget.ui
    shortcut_settings_widget.cpp
    shortcut_settings_widget.h
    shortcut_settings_widget.ui
    appearance_settings_widget.cpp
    appearance_settings_widget.h
    appearance_settings_widget.ui
    general_settings_widget.cpp
    general_settings_widget.h
    general_settings_widget.ui
    history_widget.cpp
    history_widget.h
    history_widget.ui
    history_edit_dialog.ui
    llm_post_processor.cpp
    llm_post_processor.h
    clean_text.cpp
    clean_text.h
    ocr_recognizer.cpp
    ocr_recognizer.h
    ppocrv6_ocr_recognizer.h
    speech_recognizer.h
    speech_recognizer.cpp
    local_ai_api_server.h
    local_ai_api_server.cpp
    tts_engine.h
    tts/edge_tts_engine.cpp
    tts/edge_tts_engine.h
    tts/melo_tts_engine.cpp
    tts/melo_tts_engine.h
    tts/tts_audio.cpp
    tts/tts_audio.h
    log_panel.cpp
    log_panel.h
    log_panel.ui
    logging.cpp
    logging.h
    recognition_history.cpp
    recognition_history.h
    speaker_recognizer.cpp
    speaker_recognizer.h
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
    platform_utils.h
    scroll_text_display.h
    scroll_text_display.cpp
    utils.h
    utils.cpp
)

qt_add_translations(Zenny
    LUPDATE_OPTIONS -no-obsolete
)

target_link_libraries(Zenny
    PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Multimedia
    Qt6::Network
    Qt6::Svg
    Qt6::Sql
    Qt6::WebSockets
    LibArchive::LibArchive
    sherpa-onnx-c-api
    QHotkey::QHotkey
    QCoro6::Core
    QCoro6::Network
    spdlog::spdlog_header_only
    fmt::fmt-header-only
    nlohmann_json::nlohmann_json
)

qcoro_enable_coroutines(Zenny)

if(ZENNY_PPOCRV6_ORT_TARGET AND ZENNY_PPOCRV6_OPENCV_TARGETS)
    set(ZENNY_PPOCRV6_ENABLED TRUE)
    target_sources(Zenny PRIVATE ppocrv6_ocr_recognizer.cpp)
    target_link_libraries(Zenny PRIVATE
        ${ZENNY_PPOCRV6_ORT_TARGET}
        ${ZENNY_PPOCRV6_OPENCV_TARGETS}
        Clipper2::Clipper2
    )
    target_compile_definitions(Zenny PRIVATE ZENNY_HAS_PPOCRV6)
endif()

if(WIN32)
    target_link_libraries(Zenny PRIVATE windowsapp ole32 uiautomationcore imm32)
    target_sources(Zenny PRIVATE
        windows/platform_utils.cpp
    )
elseif(APPLE)
    target_sources(Zenny PRIVATE
        macos/platform_utils.cpp
    )
elseif(UNIX)
    target_sources(Zenny PRIVATE
        linux/platform_utils.cpp
    )
endif()

target_include_directories(Zenny PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/third_parties/KDToolBox
)

target_compile_definitions(Zenny PRIVATE
    PROJECT_VERSION_STR="${PROJECT_VERSION}"
    GIT_COMMIT_ID="${GIT_COMMIT_ID}"
    GIT_COMMIT_DATE="${GIT_COMMIT_DATE}"
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
)

qt_deploy(TARGET Zenny COMPONENT Runtime)

install(TARGETS Zenny
    COMPONENT Runtime
)

# ── Development symlinks ─────────────────────────────────────────
# Create directory symlinks in the binary output so models and
# tessdata are available at runtime without manual copying.
add_custom_command(TARGET Zenny POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:Zenny>/models"
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:Zenny>/tessdata"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${PROJECT_SOURCE_DIR}/models"
        "$<TARGET_FILE_DIR:Zenny>/models"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${PROJECT_SOURCE_DIR}/teact/train_data"
        "$<TARGET_FILE_DIR:Zenny>/tessdata"
    COMMENT "Creating symlinks for models & tessdata"
)

# ── Install assets ───────────────────────────────────────────────
install(DIRECTORY ${PROJECT_SOURCE_DIR}/teact/train_data/
    DESTINATION ${CMAKE_INSTALL_BINDIR}/tessdata
    COMPONENT Runtime
)

install(DIRECTORY ${ZENNY_SENSEVOICE_MODEL_DIR}/
    DESTINATION ${CMAKE_INSTALL_BINDIR}/models/${ZENNY_SENSEVOICE_MODEL_NAME}
    COMPONENT Runtime
)

install(FILES
    ${ZENNY_PPOCRV6_DET_MODEL}
    ${ZENNY_PPOCRV6_REC_MODEL}
    ${ZENNY_PPOCRV6_KEYS}
    DESTINATION ${CMAKE_INSTALL_BINDIR}/models/ppocrv6_small
    COMPONENT Runtime
)
if(EXISTS "${ZENNY_PPOCRV6_CLS_MODEL}")
    install(FILES ${ZENNY_PPOCRV6_CLS_MODEL}
        DESTINATION ${CMAKE_INSTALL_BINDIR}/models/ppocrv6_small
        COMPONENT Runtime
    )
endif()

if(ZENNY_PPOCRV6_ENABLED AND WIN32)
    install(FILES ${ZENNY_ONNXRUNTIME_DLL}
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT Runtime
    )
    if(EXISTS "${ZENNY_ONNXRUNTIME_PROVIDERS_SHARED_DLL}")
        install(FILES ${ZENNY_ONNXRUNTIME_PROVIDERS_SHARED_DLL}
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT Runtime
        )
    endif()
    install(FILES ${ZENNY_OPENCV_WORLD_DLL}
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT Runtime
    )
endif()

install(FILES ${ZENNY_SILERO_VAD_MODEL_FILE}
    DESTINATION ${CMAKE_INSTALL_BINDIR}/models/${ZENNY_SILERO_VAD_MODEL_NAME}
    COMPONENT Runtime
)

install(FILES ${ZENNY_CAMPLUS_MODEL_FILE}
    DESTINATION ${CMAKE_INSTALL_BINDIR}/models/${ZENNY_CAMPLUS_MODEL_NAME}
    COMPONENT Runtime
)

if(WIN32)
    add_custom_command(TARGET Zenny
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:Zenny>
        $<TARGET_FILE_DIR:Zenny>
        COMMAND_EXPAND_LISTS
    )
endif()
