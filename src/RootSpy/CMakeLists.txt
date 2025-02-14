project(RootSpyExe)

# Make dictionaries
file(GLOB HEADERS Dialog*.h RSTab.h rs_mainframe.h)
ROOT_GENERATE_DICTIONARY(MyRootDict ${HEADERS} LINKDEF LinkDef.h)

file(GLOB SOURCES "*.cc")
add_executable(RootSpyExe ${SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/MyRootDict.cxx)
set_target_properties(RootSpyExe PROPERTIES OUTPUT_NAME RootSpy)

target_link_libraries(RootSpyExe RootSpy-client)
target_link_libraries(RootSpyExe ROOT::Core ROOT::RIO ROOT::Hist ROOT::Gui ROOT::GuiBld ROOT::RootAuth)
# target_link_libraries(RootSpyExe ROOT::Core ROOT::RIO ROOT::Hist ROOT::Gui)

set_target_properties(RootSpyExe PROPERTIES
    INSTALL_RPATH_USE_LINK_PATH TRUE
    BUILD_WITH_INSTALL_RPATH TRUE
)

