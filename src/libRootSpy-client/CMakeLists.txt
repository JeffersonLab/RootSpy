
project(libRootSpy-client)

add_library(RootSpy-client SHARED rs_cmsg.cc rs_xmsg.cc rs_macroutils.cc rs_info.cc rs_influxdb.cc) 

target_link_libraries(RootSpy-client ${ROOT_LIBRARIES} ${CMSG_LIBRARIES} ${xmsg_LIBRARIES} ${Protobuf_LIBRARIES} ${CURL_LIBRARIES})
set_target_properties(RootSpy PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
