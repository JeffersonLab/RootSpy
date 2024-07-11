# Project name
project(RSelog)

# Add executable for RSelog
add_executable(RSelog RSelog.cc)

# Link with libraries (assuming libRootSpy-client is built as a library)
target_link_libraries(RSelog RootSpy-client ${ROOT_LIBRARIES} ${CMSG_LIBRARIES} ${xmsg_LIBRARIES} ${Protobuf_LIBRARIES} ${CURL_LIBRARIES})
