# Project name
project(RSAchiver)

# Add executable for RArchiver
add_executable(RSArchiver HTMLOutputGenerator.cc INIReader.cc PDFOutputGenerator.cc RSArchiver.cc ini.c)

# Link with libraries (assuming libRootSpy-client is built as a library)
target_link_libraries(RSArchiver RootSpy-client ${ROOT_LIBRARIES} ${CMSG_LIBRARIES} ${xmsg_LIBRARIES} ${Protobuf_LIBRARIES} ${CURL_LIBRARIES})