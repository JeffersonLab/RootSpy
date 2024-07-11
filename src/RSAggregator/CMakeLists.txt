project(RSAggregator)

add_executable(RSAggregator RSAggregator.cc)

target_link_libraries(RSAggregator RootSpy-client RootSpy )