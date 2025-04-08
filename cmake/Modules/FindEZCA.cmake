# - Try to find EZCA (EPICS EZCA static library)
#
# Once done, this will define:
#   EZCA_FOUND        - System has EZCA
#   EZCA_INCLUDE_DIRS - Include directories for EZCA
#   EZCA_LIBRARIES    - Full paths to ezca, ca, and Com static libraries
#   HAVE_EZCA         - Defined for convenience to use in your code

find_path(EZCA_INCLUDE_DIR ezca.h
    HINTS
        $ENV{EPICS_BASE}/include
        $ENV{EPICS_BASE}/../extensions/include
)

# Detect host arch (you could enhance this by running EpicsHostArch)
set(EPICS_HOST_ARCH $ENV{EPICS_HOST_ARCH})
if(NOT EPICS_HOST_ARCH)
    execute_process(
        COMMAND uname -m
        OUTPUT_VARIABLE EPICS_HOST_ARCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

# Try to locate the libraries
find_library(EZCA_LIB libezca.a
    HINTS
        $ENV{EPICS_BASE}/lib/${EPICS_HOST_ARCH}
        $ENV{EPICS_BASE}/../extensions/lib/${EPICS_HOST_ARCH}
)

find_library(CA_LIB libca.a
    HINTS
        $ENV{EPICS_BASE}/lib/${EPICS_HOST_ARCH}
        $ENV{EPICS_BASE}/../extensions/lib/${EPICS_HOST_ARCH}
)

find_library(COM_LIB libCom.a
    HINTS
        $ENV{EPICS_BASE}/lib/${EPICS_HOST_ARCH}
        $ENV{EPICS_BASE}/../extensions/lib/${EPICS_HOST_ARCH}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(EZCA DEFAULT_MSG
    EZCA_LIB CA_LIB COM_LIB EZCA_INCLUDE_DIR
)

if(EZCA_FOUND)
    set(EZCA_INCLUDE_DIRS
        ${EZCA_INCLUDE_DIR}
    )
    set(EZCA_LIBRARIES
        ${EZCA_LIB}
        ${CA_LIB}
        ${COM_LIB}
    )
    set(HAVE_EZCA 1)
endif()

