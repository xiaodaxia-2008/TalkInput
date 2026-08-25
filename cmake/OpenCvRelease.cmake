# Official prebuilt OpenCV Windows package for the OCR test.
#
# The release archive contains all OpenCV modules. The OCR test uses only
# core/imgproc APIs, while the official Windows package exposes them through
# the combined opencv_world library.

set(ZENNY_OPENCV_VERSION "5.0.0" CACHE STRING "OpenCV version")
set(ZENNY_OPENCV_ROOT "" CACHE PATH
    "OpenCV installation root containing build/include/opencv2")
set(ZENNY_OPENCV_PACKAGE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_parties/opencv")
set(ZENNY_OPENCV_PACKAGE
    "${ZENNY_OPENCV_PACKAGE_DIR}/opencv-${ZENNY_OPENCV_VERSION}-windows.exe")
set(ZENNY_OPENCV_EXTRACT_DIR
    "${CMAKE_BINARY_DIR}/third_parties/opencv")

if(NOT ZENNY_OPENCV_ROOT)
    set(ZENNY_OPENCV_ROOT
        "${ZENNY_OPENCV_EXTRACT_DIR}/opencv/opencv/build")
endif()

if(NOT EXISTS "${ZENNY_OPENCV_ROOT}/include/opencv2/core.hpp")
    if(NOT EXISTS "${ZENNY_OPENCV_PACKAGE}")
        file(MAKE_DIRECTORY "${ZENNY_OPENCV_PACKAGE_DIR}")
        set(ZENNY_OPENCV_URL
            "https://github.com/opencv/opencv/releases/download/${ZENNY_OPENCV_VERSION}/opencv-${ZENNY_OPENCV_VERSION}-windows.exe")
        message(STATUS "Downloading OpenCV ${ZENNY_OPENCV_VERSION} from ${ZENNY_OPENCV_URL}")
        file(DOWNLOAD "${ZENNY_OPENCV_URL}" "${ZENNY_OPENCV_PACKAGE}"
             SHOW_PROGRESS STATUS ZENNY_OPENCV_DOWNLOAD_STATUS)
        list(GET ZENNY_OPENCV_DOWNLOAD_STATUS 0 ZENNY_OPENCV_DOWNLOAD_CODE)
        if(NOT ZENNY_OPENCV_DOWNLOAD_CODE EQUAL 0)
            message(FATAL_ERROR "OpenCV download failed: ${ZENNY_OPENCV_DOWNLOAD_STATUS}")
        endif()
    endif()

    find_program(ZENNY_7Z_EXECUTABLE NAMES 7z 7za)
    if(NOT ZENNY_7Z_EXECUTABLE)
        message(FATAL_ERROR
            "OpenCV prebuilt package needs 7z for extraction. "
            "Install 7-Zip or set ZENNY_OPENCV_ROOT to an extracted package.")
    endif()

    file(MAKE_DIRECTORY "${ZENNY_OPENCV_EXTRACT_DIR}")
    execute_process(
        COMMAND "${ZENNY_7Z_EXECUTABLE}" x "${ZENNY_OPENCV_PACKAGE}"
                "-o${ZENNY_OPENCV_EXTRACT_DIR}/opencv" -y
        RESULT_VARIABLE ZENNY_OPENCV_EXTRACT_RESULT
        OUTPUT_QUIET
        ERROR_VARIABLE ZENNY_OPENCV_EXTRACT_ERROR)
    if(NOT ZENNY_OPENCV_EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "OpenCV extraction failed: ${ZENNY_OPENCV_EXTRACT_ERROR}")
    endif()
endif()

set(ZENNY_OPENCV_INCLUDE_DIR "${ZENNY_OPENCV_ROOT}/include")
set(ZENNY_OPENCV_LIB_DIR "${ZENNY_OPENCV_ROOT}/x64/vc17/lib")
set(ZENNY_OPENCV_BIN_DIR "${ZENNY_OPENCV_ROOT}/x64/vc17/bin")
if(NOT EXISTS "${ZENNY_OPENCV_LIB_DIR}/opencv_world500.lib")
    set(ZENNY_OPENCV_LIB_DIR "${ZENNY_OPENCV_ROOT}/x64/vc16/lib")
    set(ZENNY_OPENCV_BIN_DIR "${ZENNY_OPENCV_ROOT}/x64/vc16/bin")
endif()

set(ZENNY_OPENCV_WORLD_LIB "${ZENNY_OPENCV_LIB_DIR}/opencv_world500.lib")
set(ZENNY_OPENCV_WORLD_DLL "${ZENNY_OPENCV_BIN_DIR}/opencv_world500.dll")

if(EXISTS "${ZENNY_OPENCV_WORLD_LIB}" AND
   EXISTS "${ZENNY_OPENCV_WORLD_DLL}")
    add_library(zenny_opencv_world SHARED IMPORTED GLOBAL)
    set_target_properties(zenny_opencv_world PROPERTIES
        IMPORTED_IMPLIB "${ZENNY_OPENCV_WORLD_LIB}"
        IMPORTED_LOCATION "${ZENNY_OPENCV_WORLD_DLL}"
        INTERFACE_INCLUDE_DIRECTORIES "${ZENNY_OPENCV_INCLUDE_DIR}")

    set(ZENNY_OPENCV_FOUND TRUE)
    message(STATUS "OpenCV ${ZENNY_OPENCV_VERSION} ready: ${ZENNY_OPENCV_ROOT}")
else()
    message(WARNING "OpenCV world library not found under ${ZENNY_OPENCV_ROOT}")
endif()
