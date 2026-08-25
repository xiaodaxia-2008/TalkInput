# Prebuilt ONNX Runtime for PP-OCRv6 C++ test (no vcpkg, no sherpa coupling)
# Downloads the official Windows x64 release package from GitHub.
# and exposes imported target `onnxruntime` (dll + lib + headers)

set(ZENNY_ONNXRUNTIME_VERSION "1.29.0")
set(ZENNY_ONNXRUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_parties/onnxruntime")
set(ZENNY_ONNXRUNTIME_ZIP "${ZENNY_ONNXRUNTIME_DIR}/onnxruntime-win-x64-${ZENNY_ONNXRUNTIME_VERSION}.zip")
set(ZENNY_ONNXRUNTIME_EXTRACT "${CMAKE_BINARY_DIR}/third_parties/onnxruntime")
set(ZENNY_ONNXRUNTIME_ROOT "${ZENNY_ONNXRUNTIME_EXTRACT}/onnxruntime-win-x64-${ZENNY_ONNXRUNTIME_VERSION}")

if(NOT EXISTS "${ZENNY_ONNXRUNTIME_ZIP}")
    file(MAKE_DIRECTORY "${ZENNY_ONNXRUNTIME_DIR}")
    set(ZENNY_ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ZENNY_ONNXRUNTIME_VERSION}/onnxruntime-win-x64-${ZENNY_ONNXRUNTIME_VERSION}.zip")
    message(STATUS "Downloading ONNX Runtime ${ZENNY_ONNXRUNTIME_VERSION} from ${ZENNY_ONNXRUNTIME_URL}")
    file(DOWNLOAD "${ZENNY_ONNXRUNTIME_URL}" "${ZENNY_ONNXRUNTIME_ZIP}" SHOW_PROGRESS)
endif()

if(NOT EXISTS "${ZENNY_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
    file(MAKE_DIRECTORY "${ZENNY_ONNXRUNTIME_EXTRACT}")
    file(ARCHIVE_EXTRACT INPUT "${ZENNY_ONNXRUNTIME_ZIP}" DESTINATION "${ZENNY_ONNXRUNTIME_EXTRACT}")
endif()

set(ZENNY_ONNXRUNTIME_INCLUDE "${ZENNY_ONNXRUNTIME_ROOT}/include")
set(ZENNY_ONNXRUNTIME_LIB "${ZENNY_ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
set(ZENNY_ONNXRUNTIME_DLL "${ZENNY_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
set(ZENNY_ONNXRUNTIME_PROVIDERS_SHARED_DLL
    "${ZENNY_ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll")
# Some releases put dll next to lib, others in same lib dir — handle both
if(NOT EXISTS "${ZENNY_ONNXRUNTIME_DLL}" AND EXISTS "${ZENNY_ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll")
    set(ZENNY_ONNXRUNTIME_DLL "${ZENNY_ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll")
endif()

if(NOT EXISTS "${ZENNY_ONNXRUNTIME_LIB}" OR NOT EXISTS "${ZENNY_ONNXRUNTIME_DLL}")
    message(WARNING "ONNX Runtime prebuilt not found at ${ZENNY_ONNXRUNTIME_ROOT} — PP-OCRv6 ONNX test will be stub")
else()
    add_library(onnxruntime SHARED IMPORTED GLOBAL)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ZENNY_ONNXRUNTIME_DLL}"
        IMPORTED_IMPLIB "${ZENNY_ONNXRUNTIME_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${ZENNY_ONNXRUNTIME_INCLUDE}"
    )
    message(STATUS "ONNX Runtime ${ZENNY_ONNXRUNTIME_VERSION} ready: ${ZENNY_ONNXRUNTIME_ROOT}")
endif()
