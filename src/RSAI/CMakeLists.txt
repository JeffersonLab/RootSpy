project(RSAI)

add_executable(RSAI RSAI.cc)

target_link_libraries(RSAI RootSpy-client)
