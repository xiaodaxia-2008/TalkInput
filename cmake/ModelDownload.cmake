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
set(TALKINPUT_PPOCRV6_MODEL_DIR
    "${PROJECT_SOURCE_DIR}/models/ppocrv6_small")
set(TALKINPUT_PPOCRV6_DET_MODEL
    "${TALKINPUT_PPOCRV6_MODEL_DIR}/det.onnx")
set(TALKINPUT_PPOCRV6_REC_MODEL
    "${TALKINPUT_PPOCRV6_MODEL_DIR}/rec.onnx")
set(TALKINPUT_PPOCRV6_CLS_MODEL
    "${TALKINPUT_PPOCRV6_MODEL_DIR}/cls.onnx")
set(TALKINPUT_PPOCRV6_KEYS
    "${TALKINPUT_PPOCRV6_MODEL_DIR}/ppocrv6_keys2.txt")

set(TALKINPUT_PPOCRV6_MODEL_URL_BASE
    "https://paddle-model-ecology.bj.bcebos.com/paddlex/official_inference_model/paddle3.0.0")
set(TALKINPUT_PPOCRV6_ARCHIVE_DIR
    "${CMAKE_BINARY_DIR}/third_parties/ppocrv6")
set(TALKINPUT_PPOCRV6_DET_ARCHIVE
    "${TALKINPUT_PPOCRV6_ARCHIVE_DIR}/PP-OCRv6_small_det_onnx_infer.tar")
set(TALKINPUT_PPOCRV6_REC_ARCHIVE
    "${TALKINPUT_PPOCRV6_ARCHIVE_DIR}/PP-OCRv6_small_rec_onnx_infer.tar")
set(TALKINPUT_PPOCRV6_DET_ARCHIVE_URL
    "${TALKINPUT_PPOCRV6_MODEL_URL_BASE}/PP-OCRv6_small_det_onnx_infer.tar")
set(TALKINPUT_PPOCRV6_REC_ARCHIVE_URL
    "${TALKINPUT_PPOCRV6_MODEL_URL_BASE}/PP-OCRv6_small_rec_onnx_infer.tar")
set(TALKINPUT_PPOCRV6_DET_ARCHIVE_SHA256
    "D218F6FBF0F1C23D2161BD6AC7F5EAA6104FA89955C09290497E31008E2618E4")
set(TALKINPUT_PPOCRV6_REC_ARCHIVE_SHA256
    "D267AB077A44A0EEDB1EA8F8C542D263F211DE8E9D7A029BF9FCFFF7E5A88FB1")
set(TALKINPUT_PPOCRV6_KEYS_URL
    "https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/main/ppocr/utils/dict/ppocrv6_dict.txt")

file(MAKE_DIRECTORY "${TALKINPUT_PPOCRV6_MODEL_DIR}")
file(MAKE_DIRECTORY "${TALKINPUT_PPOCRV6_ARCHIVE_DIR}")
if(NOT EXISTS "${TALKINPUT_PPOCRV6_DET_MODEL}")
    file(DOWNLOAD
        "${TALKINPUT_PPOCRV6_DET_ARCHIVE_URL}"
        "${TALKINPUT_PPOCRV6_DET_ARCHIVE}"
        EXPECTED_HASH "SHA256=${TALKINPUT_PPOCRV6_DET_ARCHIVE_SHA256}"
        SHOW_PROGRESS)
    file(ARCHIVE_EXTRACT INPUT "${TALKINPUT_PPOCRV6_DET_ARCHIVE}"
         DESTINATION "${TALKINPUT_PPOCRV6_ARCHIVE_DIR}")
    file(RENAME
        "${TALKINPUT_PPOCRV6_ARCHIVE_DIR}/PP-OCRv6_small_det_onnx_infer/inference.onnx"
        "${TALKINPUT_PPOCRV6_DET_MODEL}")
endif()
if(NOT EXISTS "${TALKINPUT_PPOCRV6_REC_MODEL}")
    file(DOWNLOAD
        "${TALKINPUT_PPOCRV6_REC_ARCHIVE_URL}"
        "${TALKINPUT_PPOCRV6_REC_ARCHIVE}"
        EXPECTED_HASH "SHA256=${TALKINPUT_PPOCRV6_REC_ARCHIVE_SHA256}"
        SHOW_PROGRESS)
    file(ARCHIVE_EXTRACT INPUT "${TALKINPUT_PPOCRV6_REC_ARCHIVE}"
         DESTINATION "${TALKINPUT_PPOCRV6_ARCHIVE_DIR}")
    file(RENAME
        "${TALKINPUT_PPOCRV6_ARCHIVE_DIR}/PP-OCRv6_small_rec_onnx_infer/inference.onnx"
        "${TALKINPUT_PPOCRV6_REC_MODEL}")
endif()
if(NOT EXISTS "${TALKINPUT_PPOCRV6_CLS_MODEL}")
    message(STATUS "PP-OCR cls model not found at ${TALKINPUT_PPOCRV6_CLS_MODEL} — angle classification will be disabled (optional)")
endif()
if(NOT EXISTS "${TALKINPUT_PPOCRV6_KEYS}")
    file(DOWNLOAD "${TALKINPUT_PPOCRV6_KEYS_URL}"
         "${TALKINPUT_PPOCRV6_KEYS}" SHOW_PROGRESS)
endif()

if(NOT EXISTS "${TALKINPUT_PPOCRV6_DET_MODEL}"
   OR NOT EXISTS "${TALKINPUT_PPOCRV6_REC_MODEL}"
   OR NOT EXISTS "${TALKINPUT_PPOCRV6_KEYS}")
    message(FATAL_ERROR
        "Missing PP-OCRv6 Small model files under ${TALKINPUT_PPOCRV6_MODEL_DIR}")
endif()

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

# ── Silero VAD ────────────────────────────────────────────────────
set(TALKINPUT_SILERO_VAD_MODEL_NAME
    "sherpa-onnx-silero-vad")

set(TALKINPUT_SILERO_VAD_MODEL_DIR
    "${PROJECT_SOURCE_DIR}/models/${TALKINPUT_SILERO_VAD_MODEL_NAME}")

set(TALKINPUT_SILERO_VAD_MODEL_FILE
    "${TALKINPUT_SILERO_VAD_MODEL_DIR}/silero_vad.onnx")

if(NOT EXISTS "${TALKINPUT_SILERO_VAD_MODEL_FILE}")
    set(TALKINPUT_SILERO_VAD_MODEL_URL
        "https://api.github.com/repos/snakers4/silero-vad/contents/files/silero_vad.onnx?ref=v4.0")
    message(STATUS
        "Downloading Silero VAD model from ${TALKINPUT_SILERO_VAD_MODEL_URL}")
    file(MAKE_DIRECTORY "${TALKINPUT_SILERO_VAD_MODEL_DIR}")
    file(DOWNLOAD "${TALKINPUT_SILERO_VAD_MODEL_URL}"
         "${TALKINPUT_SILERO_VAD_MODEL_FILE}"
         EXPECTED_HASH
         "SHA256=A35EBF52FD3CE5F1469B2A36158DBA761BC47B973EA3382B3186CA15B1F5AF28"
         HTTPHEADER "Accept: application/vnd.github.raw"
                   "User-Agent: TalkInput"
         SHOW_PROGRESS)
    message(STATUS "Downloaded to ${TALKINPUT_SILERO_VAD_MODEL_FILE}")
endif()

if(NOT EXISTS "${TALKINPUT_SILERO_VAD_MODEL_FILE}")
    message(FATAL_ERROR
        "Missing Silero VAD model file: ${TALKINPUT_SILERO_VAD_MODEL_FILE}")
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

# ── Speaker Recognition (CAM++) ──────────────────────────────────
set(TALKINPUT_CAMPLUS_MODEL_NAME
    "sherpa-onnx-campplus-zh-cn-16k-common")

set(TALKINPUT_CAMPLUS_MODEL_DIR
    "${PROJECT_SOURCE_DIR}/models/${TALKINPUT_CAMPLUS_MODEL_NAME}")

set(TALKINPUT_CAMPLUS_MODEL_FILE
    "${TALKINPUT_CAMPLUS_MODEL_DIR}/3dspeaker_speech_campplus_sv_zh-cn_16k-common.onnx")

if(NOT EXISTS "${TALKINPUT_CAMPLUS_MODEL_FILE}")
    set(TALKINPUT_CAMPLUS_MODEL_URL
        "https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/3dspeaker_speech_campplus_sv_zh-cn_16k-common.onnx")
    message(STATUS
        "Downloading CAM++ speaker recognition model from ${TALKINPUT_CAMPLUS_MODEL_URL}")
    file(MAKE_DIRECTORY "${TALKINPUT_CAMPLUS_MODEL_DIR}")
    file(DOWNLOAD "${TALKINPUT_CAMPLUS_MODEL_URL}"
         "${TALKINPUT_CAMPLUS_MODEL_FILE}"
         SHOW_PROGRESS)
    message(STATUS "Downloaded to ${TALKINPUT_CAMPLUS_MODEL_FILE}")
endif()

if(NOT EXISTS "${TALKINPUT_CAMPLUS_MODEL_FILE}")
    message(FATAL_ERROR
        "Missing CAM++ speaker model file: ${TALKINPUT_CAMPLUS_MODEL_FILE}")
endif()
