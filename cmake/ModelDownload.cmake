# ═══════════════════════════════════════════════════════════════════
# ModelDownload.cmake — auto-download and extract runtime models
#
# Bundles the default SenseVoice model with the build/install so
# users don't need to download it manually after installation.
# ═══════════════════════════════════════════════════════════════════

cmake_minimum_required(VERSION 3.21)

# ── PP-OCRv6 Small ───────────────────────────────────────────────
# These files are build inputs, not source-controlled assets. They are kept
# under the source tree so the install rule can bundle them into the installer.
set(ZENNY_PPOCRV6_MODEL_DIR
    "${PROJECT_SOURCE_DIR}/models/ppocrv6_small")
set(ZENNY_PPOCRV6_DET_MODEL
    "${ZENNY_PPOCRV6_MODEL_DIR}/det.onnx")
set(ZENNY_PPOCRV6_REC_MODEL
    "${ZENNY_PPOCRV6_MODEL_DIR}/rec.onnx")
set(ZENNY_PPOCRV6_CLS_MODEL
    "${ZENNY_PPOCRV6_MODEL_DIR}/cls.onnx")
set(ZENNY_PPOCRV6_KEYS
    "${ZENNY_PPOCRV6_MODEL_DIR}/ppocrv6_keys2.txt")

set(ZENNY_PPOCRV6_MODEL_URL_BASE
    "https://paddle-model-ecology.bj.bcebos.com/paddlex/official_inference_model/paddle3.0.0")
set(ZENNY_PPOCRV6_ARCHIVE_DIR
    "${CMAKE_BINARY_DIR}/third_parties/ppocrv6")
set(ZENNY_PPOCRV6_DET_ARCHIVE
    "${ZENNY_PPOCRV6_ARCHIVE_DIR}/PP-OCRv6_small_det_onnx_infer.tar")
set(ZENNY_PPOCRV6_REC_ARCHIVE
    "${ZENNY_PPOCRV6_ARCHIVE_DIR}/PP-OCRv6_small_rec_onnx_infer.tar")
set(ZENNY_PPOCRV6_DET_ARCHIVE_URL
    "${ZENNY_PPOCRV6_MODEL_URL_BASE}/PP-OCRv6_small_det_onnx_infer.tar")
set(ZENNY_PPOCRV6_REC_ARCHIVE_URL
    "${ZENNY_PPOCRV6_MODEL_URL_BASE}/PP-OCRv6_small_rec_onnx_infer.tar")
set(ZENNY_PPOCRV6_DET_ARCHIVE_SHA256
    "D218F6FBF0F1C23D2161BD6AC7F5EAA6104FA89955C09290497E31008E2618E4")
set(ZENNY_PPOCRV6_REC_ARCHIVE_SHA256
    "D267AB077A44A0EEDB1EA8F8C542D263F211DE8E9D7A029BF9FCFFF7E5A88FB1")
set(ZENNY_PPOCRV6_KEYS_URL
    "https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/main/ppocr/utils/dict/ppocrv6_dict.txt")

file(MAKE_DIRECTORY "${ZENNY_PPOCRV6_MODEL_DIR}")
file(MAKE_DIRECTORY "${ZENNY_PPOCRV6_ARCHIVE_DIR}")
if(NOT EXISTS "${ZENNY_PPOCRV6_DET_MODEL}")
    file(DOWNLOAD
        "${ZENNY_PPOCRV6_DET_ARCHIVE_URL}"
        "${ZENNY_PPOCRV6_DET_ARCHIVE}"
        EXPECTED_HASH "SHA256=${ZENNY_PPOCRV6_DET_ARCHIVE_SHA256}"
        SHOW_PROGRESS)
    file(ARCHIVE_EXTRACT INPUT "${ZENNY_PPOCRV6_DET_ARCHIVE}"
         DESTINATION "${ZENNY_PPOCRV6_ARCHIVE_DIR}")
    file(RENAME
        "${ZENNY_PPOCRV6_ARCHIVE_DIR}/PP-OCRv6_small_det_onnx_infer/inference.onnx"
        "${ZENNY_PPOCRV6_DET_MODEL}")
endif()
if(NOT EXISTS "${ZENNY_PPOCRV6_REC_MODEL}")
    file(DOWNLOAD
        "${ZENNY_PPOCRV6_REC_ARCHIVE_URL}"
        "${ZENNY_PPOCRV6_REC_ARCHIVE}"
        EXPECTED_HASH "SHA256=${ZENNY_PPOCRV6_REC_ARCHIVE_SHA256}"
        SHOW_PROGRESS)
    file(ARCHIVE_EXTRACT INPUT "${ZENNY_PPOCRV6_REC_ARCHIVE}"
         DESTINATION "${ZENNY_PPOCRV6_ARCHIVE_DIR}")
    file(RENAME
        "${ZENNY_PPOCRV6_ARCHIVE_DIR}/PP-OCRv6_small_rec_onnx_infer/inference.onnx"
        "${ZENNY_PPOCRV6_REC_MODEL}")
endif()
if(NOT EXISTS "${ZENNY_PPOCRV6_CLS_MODEL}")
    message(STATUS "PP-OCR cls model not found at ${ZENNY_PPOCRV6_CLS_MODEL} — angle classification will be disabled (optional)")
endif()
if(NOT EXISTS "${ZENNY_PPOCRV6_KEYS}")
    file(DOWNLOAD "${ZENNY_PPOCRV6_KEYS_URL}"
         "${ZENNY_PPOCRV6_KEYS}" SHOW_PROGRESS)
endif()

if(NOT EXISTS "${ZENNY_PPOCRV6_DET_MODEL}"
   OR NOT EXISTS "${ZENNY_PPOCRV6_REC_MODEL}"
   OR NOT EXISTS "${ZENNY_PPOCRV6_KEYS}")
    message(FATAL_ERROR
        "Missing PP-OCRv6 Small model files under ${ZENNY_PPOCRV6_MODEL_DIR}")
endif()

# ── SenseVoice (default ASR model) ───────────────────────────────
set(ZENNY_SENSEVOICE_MODEL_NAME
    "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17")

set(ZENNY_SENSEVOICE_MODEL_DIR
    "${PROJECT_SOURCE_DIR}/models/${ZENNY_SENSEVOICE_MODEL_NAME}")

set(ZENNY_SENSEVOICE_MODEL_ARCHIVE
    "${PROJECT_SOURCE_DIR}/models/${ZENNY_SENSEVOICE_MODEL_NAME}.tar.bz2")

if(NOT EXISTS "${ZENNY_SENSEVOICE_MODEL_ARCHIVE}")
    set(ZENNY_SENSEVOICE_MODEL_URL
        "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/${ZENNY_SENSEVOICE_MODEL_NAME}.tar.bz2")
    message(STATUS
        "Downloading SenseVoice model from ${ZENNY_SENSEVOICE_MODEL_URL}")
    file(DOWNLOAD "${ZENNY_SENSEVOICE_MODEL_URL}"
         "${ZENNY_SENSEVOICE_MODEL_ARCHIVE}"
         SHOW_PROGRESS)
    message(STATUS "Downloaded to ${ZENNY_SENSEVOICE_MODEL_ARCHIVE}")
endif()

# ── Silero VAD ────────────────────────────────────────────────────
set(ZENNY_SILERO_VAD_MODEL_NAME
    "sherpa-onnx-silero-vad")

set(ZENNY_SILERO_VAD_MODEL_DIR
    "${PROJECT_SOURCE_DIR}/models/${ZENNY_SILERO_VAD_MODEL_NAME}")

set(ZENNY_SILERO_VAD_MODEL_FILE
    "${ZENNY_SILERO_VAD_MODEL_DIR}/silero_vad.onnx")

if(NOT EXISTS "${ZENNY_SILERO_VAD_MODEL_FILE}")
    set(ZENNY_SILERO_VAD_MODEL_URL
        "https://api.github.com/repos/snakers4/silero-vad/contents/files/silero_vad.onnx?ref=v4.0")
    message(STATUS
        "Downloading Silero VAD model from ${ZENNY_SILERO_VAD_MODEL_URL}")
    file(MAKE_DIRECTORY "${ZENNY_SILERO_VAD_MODEL_DIR}")
    file(DOWNLOAD "${ZENNY_SILERO_VAD_MODEL_URL}"
         "${ZENNY_SILERO_VAD_MODEL_FILE}"
         EXPECTED_HASH
         "SHA256=A35EBF52FD3CE5F1469B2A36158DBA761BC47B973EA3382B3186CA15B1F5AF28"
         HTTPHEADER "Accept: application/vnd.github.raw"
                   "User-Agent: Zenny"
         SHOW_PROGRESS)
    message(STATUS "Downloaded to ${ZENNY_SILERO_VAD_MODEL_FILE}")
endif()

if(NOT EXISTS "${ZENNY_SILERO_VAD_MODEL_FILE}")
    message(FATAL_ERROR
        "Missing Silero VAD model file: ${ZENNY_SILERO_VAD_MODEL_FILE}")
endif()

if(NOT EXISTS "${ZENNY_SENSEVOICE_MODEL_DIR}")
    message(STATUS "Extracting ${ZENNY_SENSEVOICE_MODEL_ARCHIVE}")
    file(ARCHIVE_EXTRACT
        INPUT "${ZENNY_SENSEVOICE_MODEL_ARCHIVE}"
        DESTINATION "${PROJECT_SOURCE_DIR}/models"
    )
endif()

if(NOT EXISTS "${ZENNY_SENSEVOICE_MODEL_DIR}/model.int8.onnx")
    message(FATAL_ERROR
        "Missing SenseVoice model files under ${ZENNY_SENSEVOICE_MODEL_DIR}")
endif()

# ── Speaker Recognition (CAM++) ──────────────────────────────────
set(ZENNY_CAMPLUS_MODEL_NAME
    "sherpa-onnx-campplus-zh-cn-16k-common")

set(ZENNY_CAMPLUS_MODEL_DIR
    "${PROJECT_SOURCE_DIR}/models/${ZENNY_CAMPLUS_MODEL_NAME}")

set(ZENNY_CAMPLUS_MODEL_FILE
    "${ZENNY_CAMPLUS_MODEL_DIR}/3dspeaker_speech_campplus_sv_zh-cn_16k-common.onnx")

if(NOT EXISTS "${ZENNY_CAMPLUS_MODEL_FILE}")
    set(ZENNY_CAMPLUS_MODEL_URL
        "https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/3dspeaker_speech_campplus_sv_zh-cn_16k-common.onnx")
    message(STATUS
        "Downloading CAM++ speaker recognition model from ${ZENNY_CAMPLUS_MODEL_URL}")
    file(MAKE_DIRECTORY "${ZENNY_CAMPLUS_MODEL_DIR}")
    file(DOWNLOAD "${ZENNY_CAMPLUS_MODEL_URL}"
         "${ZENNY_CAMPLUS_MODEL_FILE}"
         SHOW_PROGRESS)
    message(STATUS "Downloaded to ${ZENNY_CAMPLUS_MODEL_FILE}")
endif()

if(NOT EXISTS "${ZENNY_CAMPLUS_MODEL_FILE}")
    message(FATAL_ERROR
        "Missing CAM++ speaker model file: ${ZENNY_CAMPLUS_MODEL_FILE}")
endif()
