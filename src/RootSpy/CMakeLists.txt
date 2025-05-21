project(RootSpyExe)

set(READLINE_LIBRARY /usr/lib64/libreadline.so.8)

# Make dictionaries
file(GLOB HEADERS Dialog*.h RSTab.h rs_mainframe.h)
ROOT_GENERATE_DICTIONARY(MyRootDict ${HEADERS} LINKDEF LinkDef.h)

file(GLOB SOURCES "*.cc")
add_executable(RootSpyExe ${SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/MyRootDict.cxx)
set_target_properties(RootSpyExe PROPERTIES OUTPUT_NAME RootSpy)

target_link_libraries(RootSpyExe
  PRIVATE
    RootSpy-client
    ROOT::Core ROOT::RIO ROOT::Hist ROOT::Gui ROOT::GuiBld ROOT::RootAuth
    ${EZCA_LIBRARIES}
    ${READLINE_LIBRARY}
)

set_target_properties(RootSpyExe PROPERTIES
    INSTALL_RPATH_USE_LINK_PATH TRUE
    BUILD_WITH_INSTALL_RPATH TRUE
)

