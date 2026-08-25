set(ZENNY_SHERPA_ONNX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_parties/sherpa-onnx")

if(CMAKE_BUILD_TYPE MATCHES "Debug")
    set(ZENNY_SHERPA_ONNX_CONFIG "Debug")
else()
    set(ZENNY_SHERPA_ONNX_CONFIG "Release")
endif()

set(ZENNY_SHERPA_ONNX_PACKAGE
    "${ZENNY_SHERPA_ONNX_DIR}/sherpa-onnx-v1.13.3-win-x64-static-MD-${ZENNY_SHERPA_ONNX_CONFIG}-lib.tar.bz2")
set(ZENNY_SHERPA_ONNX_EXTRACT_DIR
    "${CMAKE_BINARY_DIR}/third_parties/sherpa-onnx")
set(ZENNY_SHERPA_ONNX_ROOT
    "${ZENNY_SHERPA_ONNX_EXTRACT_DIR}/sherpa-onnx-v1.13.3-win-x64-static-MD-${ZENNY_SHERPA_ONNX_CONFIG}-lib")
set(ZENNY_SHERPA_ONNX_LIB_DIR
    "${ZENNY_SHERPA_ONNX_ROOT}/lib")
set(ZENNY_SHERPA_ONNX_INCLUDE_DIR
    "${ZENNY_SHERPA_ONNX_DIR}/include")

if(NOT EXISTS "${ZENNY_SHERPA_ONNX_PACKAGE}")
    set(ZENNY_SHERPA_ONNX_URL
        "https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.13.3/sherpa-onnx-v1.13.3-win-x64-static-MD-${ZENNY_SHERPA_ONNX_CONFIG}-lib.tar.bz2")
    message(STATUS "Downloading sherpa-onnx from ${ZENNY_SHERPA_ONNX_URL}")
    file(DOWNLOAD "${ZENNY_SHERPA_ONNX_URL}"
         "${ZENNY_SHERPA_ONNX_PACKAGE}"
         SHOW_PROGRESS)
    message(STATUS "Downloaded to ${ZENNY_SHERPA_ONNX_PACKAGE}")
endif()

if(NOT EXISTS "${ZENNY_SHERPA_ONNX_ROOT}")
    message(STATUS "Extracting ${ZENNY_SHERPA_ONNX_PACKAGE}")
    file(MAKE_DIRECTORY "${ZENNY_SHERPA_ONNX_EXTRACT_DIR}")
    file(ARCHIVE_EXTRACT
        INPUT "${ZENNY_SHERPA_ONNX_PACKAGE}"
        DESTINATION "${ZENNY_SHERPA_ONNX_EXTRACT_DIR}"
    )
endif()

if(NOT EXISTS "${ZENNY_SHERPA_ONNX_INCLUDE_DIR}/sherpa-onnx/c-api/c-api.h")
    message(FATAL_ERROR "Missing sherpa-onnx C API header under ${ZENNY_SHERPA_ONNX_INCLUDE_DIR}")
endif()

set(ZENNY_SHERPA_ONNX_LIBS
    "${ZENNY_SHERPA_ONNX_LIB_DIR}/sherpa-onnx-c-api.lib"
    "${ZENNY_SHERPA_ONNX_LIB_DIR}/sherpa-onnx-cxx-api.lib"
    "${ZENNY_SHERPA_ONNX_LIB_DIR}/sherpa-onnx-core.lib"
    "${ZENNY_SHERPA_ONNX_LIB_DIR}/sherpa-onnx-kaldifst-core.lib"
    "${ZENNY_SHERPA_ONNX_LIB_DIR}/sherpa-onnx-fstfar.lib"
    "${ZENNY_SHERPA_ONNX_LIB_DIR}/sherpa-onnx-fst.lib"
    "${ZENNY_SHERPA_ONNX_LIB_DIR}/kaldi-decoder-core.lib"
    "${ZENNY_SHERPA_ONNX_LIB_DIR}/kaldi-native-fbank-core.lib"
    "${ZENNY_SHERPA_ONNX_LIB_DIR}/ssentencepiece_core.lib"
    "${ZENNY_SHERPA_ONNX_LIB_DIR}/kissfft-float.lib"
    "${ZENNY_SHERPA_ONNX_LIB_DIR}/onnxruntime.lib"
    "${ZENNY_SHERPA_ONNX_LIB_DIR}/ucd.lib"
    "${ZENNY_SHERPA_ONNX_LIB_DIR}/espeak-ng.lib"
    "${ZENNY_SHERPA_ONNX_LIB_DIR}/piper_phonemize.lib"
)

foreach(lib IN LISTS ZENNY_SHERPA_ONNX_LIBS)
    if(NOT EXISTS "${lib}")
        message(FATAL_ERROR "Missing sherpa-onnx library: ${lib}")
    endif()
endforeach()

add_library(sherpa-onnx-c-api INTERFACE IMPORTED GLOBAL)
target_include_directories(sherpa-onnx-c-api INTERFACE
    "${ZENNY_SHERPA_ONNX_INCLUDE_DIR}"
)
target_link_libraries(sherpa-onnx-c-api INTERFACE
    ${ZENNY_SHERPA_ONNX_LIBS}
    ws2_32
    bcrypt
    advapi32
    userenv
)
