# Download/extract the RapidOcrOnnx C library (clib) and PP-OCR models.
#
# The clib package statically links onnxruntime + opencv into a single
# RapidOcrOnnx.dll, so consumers only need the .dll, the import .lib and the
# C API header. See https://github.com/RapidAI/RapidOcrOnnx
#
# Variables produced:
#   TALKINPUT_RAPID_OCR_DLL           path to RapidOcrOnnx.dll
#   TALKINPUT_RAPID_OCR_LIB           path to RapidOcrOnnx.lib
#   TALKINPUT_RAPID_OCR_INCLUDE_DIR   C API header directory
#   TALKINPUT_RAPID_OCR_MODEL_DIR     directory with the PP-OCR onnx models + keys
#   rapidocr-onnx-c-api               IMPORTED target (dll + lib + includes)

set(TALKINPUT_RAPID_OCR_VERSION "1.2.2")
set(TALKINPUT_RAPID_OCR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_parties/rapid-ocr")
set(TALKINPUT_RAPID_OCR_CLIB_PACKAGE
    "${TALKINPUT_RAPID_OCR_DIR}/RapidOcrOnnx-${TALKINPUT_RAPID_OCR_VERSION}-windows-clib.7z")
set(TALKINPUT_RAPID_OCR_EXTRACT_DIR
    "${CMAKE_BINARY_DIR}/third_parties/rapid-ocr")
set(TALKINPUT_RAPID_OCR_ROOT
    "${TALKINPUT_RAPID_OCR_EXTRACT_DIR}/windows-clib/win-CLIB-CPU-x64")
set(TALKINPUT_RAPID_OCR_DLL
    "${TALKINPUT_RAPID_OCR_ROOT}/bin/RapidOcrOnnx.dll")
set(TALKINPUT_RAPID_OCR_LIB
    "${TALKINPUT_RAPID_OCR_ROOT}/lib/RapidOcrOnnx.lib")
set(TALKINPUT_RAPID_OCR_INCLUDE_DIR
    "${TALKINPUT_RAPID_OCR_ROOT}/include")

if(NOT EXISTS "${TALKINPUT_RAPID_OCR_CLIB_PACKAGE}")
    file(MAKE_DIRECTORY "${TALKINPUT_RAPID_OCR_DIR}")
    set(TALKINPUT_RAPID_OCR_CLIB_URL
        "https://github.com/RapidAI/RapidOcrOnnx/releases/download/${TALKINPUT_RAPID_OCR_VERSION}/windows-clib.7z")
    message(STATUS "Downloading RapidOcrOnnx clib from ${TALKINPUT_RAPID_OCR_CLIB_URL}")
    file(DOWNLOAD "${TALKINPUT_RAPID_OCR_CLIB_URL}"
         "${TALKINPUT_RAPID_OCR_CLIB_PACKAGE}"
         SHOW_PROGRESS)
    message(STATUS "Downloaded to ${TALKINPUT_RAPID_OCR_CLIB_PACKAGE}")
endif()

if(NOT EXISTS "${TALKINPUT_RAPID_OCR_DLL}")
    file(MAKE_DIRECTORY "${TALKINPUT_RAPID_OCR_EXTRACT_DIR}")
    file(ARCHIVE_EXTRACT
        INPUT "${TALKINPUT_RAPID_OCR_CLIB_PACKAGE}"
        DESTINATION "${TALKINPUT_RAPID_OCR_EXTRACT_DIR}"
    )
endif()

# PP-OCRv3 models for the demo/test. The init release's models.7z carries
# det/cls/rec onnx files; the keys file is fetched from the repo.
set(TALKINPUT_RAPID_OCR_MODEL_DIR "${TALKINPUT_RAPID_OCR_DIR}/models")
set(TALKINPUT_RAPID_OCR_MODEL_PACKAGE
    "${TALKINPUT_RAPID_OCR_MODEL_DIR}/PP-OCRv3-onnx-models.7z")
set(TALKINPUT_RAPID_OCR_DET_MODEL
    "${TALKINPUT_RAPID_OCR_MODEL_DIR}/ch_PP-OCRv3_det_infer.onnx")
set(TALKINPUT_RAPID_OCR_CLS_MODEL
    "${TALKINPUT_RAPID_OCR_MODEL_DIR}/ch_ppocr_mobile_v2.0_cls_infer.onnx")
set(TALKINPUT_RAPID_OCR_REC_MODEL
    "${TALKINPUT_RAPID_OCR_MODEL_DIR}/ch_PP-OCRv3_rec_infer.onnx")
set(TALKINPUT_RAPID_OCR_KEYS
    "${TALKINPUT_RAPID_OCR_MODEL_DIR}/ppocr_keys_v1.txt")

if(NOT EXISTS "${TALKINPUT_RAPID_OCR_DET_MODEL}"
   OR NOT EXISTS "${TALKINPUT_RAPID_OCR_CLS_MODEL}"
   OR NOT EXISTS "${TALKINPUT_RAPID_OCR_REC_MODEL}")
    file(MAKE_DIRECTORY "${TALKINPUT_RAPID_OCR_MODEL_DIR}")
    if(NOT EXISTS "${TALKINPUT_RAPID_OCR_MODEL_PACKAGE}")
        set(TALKINPUT_RAPID_OCR_MODELS_URL
            "https://github.com/RapidAI/RapidOcrOnnx/releases/download/init/models.7z")
        message(STATUS "Downloading PP-OCR models from ${TALKINPUT_RAPID_OCR_MODELS_URL}")
        file(DOWNLOAD "${TALKINPUT_RAPID_OCR_MODELS_URL}"
             "${TALKINPUT_RAPID_OCR_MODEL_PACKAGE}"
             SHOW_PROGRESS)
    endif()
    file(ARCHIVE_EXTRACT
        INPUT "${TALKINPUT_RAPID_OCR_MODEL_PACKAGE}"
        DESTINATION "${TALKINPUT_RAPID_OCR_MODEL_DIR}"
    )
endif()

if(NOT EXISTS "${TALKINPUT_RAPID_OCR_KEYS}")
    file(DOWNLOAD
         "https://raw.githubusercontent.com/RapidAI/RapidOcrOnnx/${TALKINPUT_RAPID_OCR_VERSION}/models/ppocr_keys_v1.txt"
         "${TALKINPUT_RAPID_OCR_KEYS}"
         SHOW_PROGRESS)
endif()

foreach(file IN ITEMS
        TALKINPUT_RAPID_OCR_DLL
        TALKINPUT_RAPID_OCR_LIB
        TALKINPUT_RAPID_OCR_DET_MODEL
        TALKINPUT_RAPID_OCR_CLS_MODEL
        TALKINPUT_RAPID_OCR_REC_MODEL
        TALKINPUT_RAPID_OCR_KEYS)
    if(NOT EXISTS "${${file}}")
        message(FATAL_ERROR "Missing RapidOcrOnnx artifact: ${${file}}")
    endif()
endforeach()

add_library(rapidocr-onnx-c-api SHARED IMPORTED GLOBAL)
set_target_properties(rapidocr-onnx-c-api PROPERTIES
    IMPORTED_LOCATION "${TALKINPUT_RAPID_OCR_DLL}"
    IMPORTED_IMPLIB "${TALKINPUT_RAPID_OCR_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${TALKINPUT_RAPID_OCR_INCLUDE_DIR}"
)