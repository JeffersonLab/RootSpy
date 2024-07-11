
project(libRootSpy-client)

# Define the library target
add_library(RootSpy-client rs_cmsg.cc rs_xmsg.cc rs_macroutils.cc rs_info.cc rs_influxdb.cc) 

# Assuming you have a target named 'your_target_name'
target_compile_options(RootSpy-client PRIVATE -fpermissive)

target_link_libraries(RootSpy-client ${ROOT_LIBRARIES} ${CMSG_LIBRARIES} ${xmsg_LIBRARIES} ${Protobuf_LIBRARIES} ${Protobuf_LITE_LIBRARIES})