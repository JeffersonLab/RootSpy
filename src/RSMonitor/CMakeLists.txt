# Project name
project(RSMonitor)

# Add executable for RSMonitor
add_executable(RSMonitor RSMonitor.cc rsmon_cmsg.cc)

# Link with libraries (assuming libRootSpy-client is built as a library)
target_link_libraries(RSMonitor RootSpy-client ${ROOT_LIBRARIES} ${CMSG_LIBRARIES} ${xmsg_LIBRARIES} ${Protobuf_LIBRARIES} ${CURL_LIBRARIES})
