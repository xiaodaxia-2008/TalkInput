# ═══════════════════════════════════════════════════════════════════
# ModelDownload.cmake — auto-download and extract ASR models
#
# Bundles the default SenseVoice model with the build/install so
# users don't need to download it manually after installation.
# ═══════════════════════════════════════════════════════════════════

cmake_minimum_required(VERSION 3.21)

# ── SenseVoice (default ASR model) ───────────────────────────────
set(TALKINPUT_SENSEVOICE_MODEL_NAME
    "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17")

set(TALKINPUT_SENSEVOICE_MODEL_DIR
    "${PROJECT_SOURCE_DIR}/models/${TALKINPUT_SENSEVOICE_MODEL_NAME}")

set(TALKINPUT_SENSEVOICE_MODEL_ARCHIVE
    "${PROJECT_SOURCE_DIR}/models/${TALKINPUT_SENSEVOICE_MODEL_NAME}.tar.bz2")

if(NOT EXISTS "${TALKINPUT_SENSEVOICE_MODEL_ARCHIVE}")
    set(TALKINPUT_SENSEVOICE_MODEL_URL
        "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/${TALKINPUT_SENSEVOICE_MODEL_NAME}.tar.bz2")
    message(STATUS
        "Downloading SenseVoice model from ${TALKINPUT_SENSEVOICE_MODEL_URL}")
    file(DOWNLOAD "${TALKINPUT_SENSEVOICE_MODEL_URL}"
         "${TALKINPUT_SENSEVOICE_MODEL_ARCHIVE}"
         SHOW_PROGRESS)
    message(STATUS "Downloaded to ${TALKINPUT_SENSEVOICE_MODEL_ARCHIVE}")
endif()

if(NOT EXISTS "${TALKINPUT_SENSEVOICE_MODEL_DIR}")
    message(STATUS "Extracting ${TALKINPUT_SENSEVOICE_MODEL_ARCHIVE}")
    file(ARCHIVE_EXTRACT
        INPUT "${TALKINPUT_SENSEVOICE_MODEL_ARCHIVE}"
        DESTINATION "${PROJECT_SOURCE_DIR}/models"
    )
endif()

if(NOT EXISTS "${TALKINPUT_SENSEVOICE_MODEL_DIR}/model.int8.onnx")
    message(FATAL_ERROR
        "Missing SenseVoice model files under ${TALKINPUT_SENSEVOICE_MODEL_DIR}")
endif()
