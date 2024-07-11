# Project name
project(RSAggregator)

# Add executable for RSelog
add_executable(RSAggregator RSAggregator.cc)

# Link with libraries (assuming libRootSpy-client is built as a library)
target_link_libraries(RSAggregator RootSpy-client RootSpy ${ROOT_LIBRARIES} ${CMSG_LIBRARIES} ${xmsg_LIBRARIES} ${Protobuf_LIBRARIES} ${CURL_LIBRARIES})