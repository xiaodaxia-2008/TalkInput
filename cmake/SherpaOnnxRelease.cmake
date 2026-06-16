set(TALKINPUT_SHERPA_ONNX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParties/sherpa-onnx")

if(CMAKE_BUILD_TYPE MATCHES "Debug")
    set(TALKINPUT_SHERPA_ONNX_CONFIG "Debug")
else()
    set(TALKINPUT_SHERPA_ONNX_CONFIG "Release")
endif()

set(TALKINPUT_SHERPA_ONNX_PACKAGE
    "${TALKINPUT_SHERPA_ONNX_DIR}/sherpa-onnx-v1.13.3-win-x64-static-MD-${TALKINPUT_SHERPA_ONNX_CONFIG}-lib.tar.bz2")
set(TALKINPUT_SHERPA_ONNX_EXTRACT_DIR
    "${CMAKE_BINARY_DIR}/ThirdParties/sherpa-onnx")
set(TALKINPUT_SHERPA_ONNX_ROOT
    "${TALKINPUT_SHERPA_ONNX_EXTRACT_DIR}/sherpa-onnx-v1.13.3-win-x64-static-MD-${TALKINPUT_SHERPA_ONNX_CONFIG}-lib")
set(TALKINPUT_SHERPA_ONNX_LIB_DIR
    "${TALKINPUT_SHERPA_ONNX_ROOT}/lib")
set(TALKINPUT_SHERPA_ONNX_INCLUDE_DIR
    "${TALKINPUT_SHERPA_ONNX_DIR}/include")

if(NOT EXISTS "${TALKINPUT_SHERPA_ONNX_PACKAGE}")
    set(TALKINPUT_SHERPA_ONNX_URL
        "https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.13.3/sherpa-onnx-v1.13.3-win-x64-static-MD-${TALKINPUT_SHERPA_ONNX_CONFIG}-lib.tar.bz2")
    message(STATUS "Downloading sherpa-onnx from ${TALKINPUT_SHERPA_ONNX_URL}")
    file(DOWNLOAD "${TALKINPUT_SHERPA_ONNX_URL}"
         "${TALKINPUT_SHERPA_ONNX_PACKAGE}"
         SHOW_PROGRESS)
    message(STATUS "Downloaded to ${TALKINPUT_SHERPA_ONNX_PACKAGE}")
endif()

if(NOT EXISTS "${TALKINPUT_SHERPA_ONNX_ROOT}")
    message(STATUS "Extracting ${TALKINPUT_SHERPA_ONNX_PACKAGE}")
    file(MAKE_DIRECTORY "${TALKINPUT_SHERPA_ONNX_EXTRACT_DIR}")
    file(ARCHIVE_EXTRACT
        INPUT "${TALKINPUT_SHERPA_ONNX_PACKAGE}"
        DESTINATION "${TALKINPUT_SHERPA_ONNX_EXTRACT_DIR}"
    )
endif()

if(NOT EXISTS "${TALKINPUT_SHERPA_ONNX_INCLUDE_DIR}/sherpa-onnx/c-api/c-api.h")
    message(FATAL_ERROR "Missing sherpa-onnx C API header under ${TALKINPUT_SHERPA_ONNX_INCLUDE_DIR}")
endif()

set(TALKINPUT_SHERPA_ONNX_LIBS
    "${TALKINPUT_SHERPA_ONNX_LIB_DIR}/sherpa-onnx-c-api.lib"
    "${TALKINPUT_SHERPA_ONNX_LIB_DIR}/sherpa-onnx-cxx-api.lib"
    "${TALKINPUT_SHERPA_ONNX_LIB_DIR}/sherpa-onnx-core.lib"
    "${TALKINPUT_SHERPA_ONNX_LIB_DIR}/sherpa-onnx-kaldifst-core.lib"
    "${TALKINPUT_SHERPA_ONNX_LIB_DIR}/sherpa-onnx-fstfar.lib"
    "${TALKINPUT_SHERPA_ONNX_LIB_DIR}/sherpa-onnx-fst.lib"
    "${TALKINPUT_SHERPA_ONNX_LIB_DIR}/kaldi-decoder-core.lib"
    "${TALKINPUT_SHERPA_ONNX_LIB_DIR}/kaldi-native-fbank-core.lib"
    "${TALKINPUT_SHERPA_ONNX_LIB_DIR}/ssentencepiece_core.lib"
    "${TALKINPUT_SHERPA_ONNX_LIB_DIR}/kissfft-float.lib"
    "${TALKINPUT_SHERPA_ONNX_LIB_DIR}/onnxruntime.lib"
    "${TALKINPUT_SHERPA_ONNX_LIB_DIR}/ucd.lib"
    "${TALKINPUT_SHERPA_ONNX_LIB_DIR}/espeak-ng.lib"
    "${TALKINPUT_SHERPA_ONNX_LIB_DIR}/piper_phonemize.lib"
)

foreach(lib IN LISTS TALKINPUT_SHERPA_ONNX_LIBS)
    if(NOT EXISTS "${lib}")
        message(FATAL_ERROR "Missing sherpa-onnx library: ${lib}")
    endif()
endforeach()

add_library(sherpa-onnx-c-api INTERFACE IMPORTED GLOBAL)
target_include_directories(sherpa-onnx-c-api INTERFACE
    "${TALKINPUT_SHERPA_ONNX_INCLUDE_DIR}"
)
target_link_libraries(sherpa-onnx-c-api INTERFACE
    ${TALKINPUT_SHERPA_ONNX_LIBS}
    ws2_32
    bcrypt
    advapi32
    userenv
)
