project(RSAI)

add_executable(RSAI RSAI.cc)

target_link_libraries(RSAI RootSpy-client)
set_target_properties(RSAI PROPERTIES
    INSTALL_RPATH_USE_LINK_PATH TRUE
    BUILD_WITH_INSTALL_RPATH TRUE
)
