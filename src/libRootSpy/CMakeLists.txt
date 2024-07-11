project(libRootSpy)

add_library(RootSpy SHARED DRootSpy.cc)

target_link_libraries(RootSpy ${ROOT_LIBRARIES} ${CMSG_LIBRARIES} ${xmsg_LIBRARIES} ${Protobuf_LIBRARIES})
