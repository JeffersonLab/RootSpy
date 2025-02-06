cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0074 NEW)
project(RootSpy)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)

# Find required packages
find_package(xmsg REQUIRED)
find_package(ROOT REQUIRED COMPONENTS Core RIO Hist Gui GuiBld RootAuth)
find_package(Protobuf REQUIRED)
find_package(ZeroMQ REQUIRED)
find_package(CURL REQUIRED)

# cmsg
set(CMSG_ROOT "$ENV{cmsg_ROOT}")
if (NOT CMSG_ROOT)
    message(FATAL_ERROR "Environment variable cmsg_ROOT is not set.")
endif()
set(ARCH ${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR})  # This line copied from cMsg CMakeLists.txt
set(CMSG_INCLUDE_DIR "${CMSG_ROOT}/${ARCH}/include")
set(CMSG_LIBRARY_DIR "${CMSG_ROOT}/${ARCH}/lib")
add_compile_definitions(HAVE_CMSG=1)
set(CMSG_LIBRARIES cmsgRegex  cMsg  cMsgxx)
set(xmsg_LIBRARIES xmsg)

# Ensure symbols are not hidden so cling can find them
set(CMAKE_EXE_LINKER_FLAGS "-Wl,--export-dynamic")

# For GCC and Clang, disable deprecated declarations warnings globally.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wno-deprecated-declarations)
endif()

# Include directories
include_directories(${ROOT_INCLUDE_DIRS})
include_directories(${Protobuf_INCLUDE_DIR})
include_directories(${xmsg_DIR}/../../../include)
include_directories(${CMSG_INCLUDE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/libRootSpy-client)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/libRootSpy)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/include)

# Library directories
link_directories(${ROOT_LIBRARY_DIR})
link_directories(${CMSG_LIBRARY_DIR})
link_directories(${xmsg_DIR}/../..)

add_subdirectory(src/libRootSpy)
add_subdirectory(src/libRootSpy-client)
add_subdirectory(src/RSAI)
add_subdirectory(src/RSTimeSeries)
add_subdirectory(src/RSelog)
add_subdirectory(src/RSAggregator)
add_subdirectory(src/RSArchiver)
add_subdirectory(src/RootSpy)
# add_subdirectory(src/RSMonitor)
# add_subdirectory(src/RSDumpTree)

# Install targets
install(TARGETS RootSpyExe RootSpy RootSpy-client RSAI RSTimeSeries RSelog RSAggregator RSArchiver
    RUNTIME DESTINATION bin               # For executables
    LIBRARY DESTINATION lib               # For shared libraries
    ARCHIVE DESTINATION lib               # For static libraries
)

# Glob all headers matching the pattern src/lib*/*.h and install them
file(GLOB LIB_HEADERS "src/lib*/*.h")
install(FILES ${LIB_HEADERS} DESTINATION include)

# Set the CMAKE_INSTALL_PREFIX if not set
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX /usr/local)
endif()
