# Project name
project(RSTimeSeries)

# Add executable for RSTimeSeries
add_executable(RSTimeSeries RSTimeSeries.cc)

# Link with libraries (assuming libRootSpy-client is built as a library)
target_link_libraries(RSTimeSeries RootSpy-client ${ROOT_LIBRARIES} ${CMSG_LIBRARIES} ${xmsg_LIBRARIES} ${Protobuf_LIBRARIES} ${CURL_LIBRARIES})
