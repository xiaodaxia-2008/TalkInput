if (CMAKE_VERSION VERSION_LESS 3.1.0)
    message(FATAL_ERROR \"QCoro6 requires at least CMake version 3.1.0\")
endif()

if (NOT QCoro6_FIND_COMPONENTS)
    set(QCoro6_NOT_FOUND_MESSAGE "The QCoro6 package requires at least one component")
    set(QCoro6_FOUND FALSE)
    return()
endif()

set(_QCoro_FIND_PARTS_REQUIRED)
if (QCoro6_FIND_REQUIRED)
    set(_QCoro_FIND_PARTS_REQUIRED REQUIRED)
endif()
set(_QCoro_FIND_PARTS_QUIET)
if (QCoro6_FIND_QUIET)
    set(_QCoro_FIND_PARTS_QUIET QUIET)
endif()

get_filename_component(_qcoro_install_prefix "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(_QCoro_NOTFOUND_MESSAGE)

foreach(module ${QCoro6_FIND_COMPONENTS})
    find_package(QCoro6${module}
        ${_QCoro_FIND_PARTS_QUIET}
        ${_QCoro_FIND_PARTS_REQUIRED}
        PATHS ${_qcoro_install_prefix} NO_DEFAULT_PATH
    )
    if (NOT QCoro6${module}_FOUND)
       if (QCoro6_FIND_REQUIRED_${module})
           set(_QCoro_NOTFOUND_MESSAGE "${_QCoro_NOTFOUND_MESSAGE}Failed to find QCoro component \"${module}\" config file at \"${_qcoro_install_prefix}\"\n")
       elseif (NOT QCoro6_FIND_QUIETLY)
           message(WARNING "Failed to find QCoro6 component \"${module}\" config file at \"${_qcoro_install_prefix}\"")
       endif()
    endif()
endforeach()

if (_QCoro_NOTFOUND_MESSAGE)
    set(QCoro6_NOT_FOUND_MESSAGE "${_QCoro_NOTFOUND_MESSAGE}")
    set(QCoro6_FOUND FALSE)
endif()

