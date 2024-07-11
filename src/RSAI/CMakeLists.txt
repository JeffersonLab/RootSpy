# Minimum required CMake version
cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0074 NEW)

# Project name
project(RSAI)

# Add executable for rsai
# add_executable(RSAI RSAI.cc)
add_executable(RSAI RSAI.cc ../libRootSpy-client/rs_cmsg.cc ../libRootSpy-client/rs_xmsg.cc ../libRootSpy-client/rs_macroutils.cc ../libRootSpy-client/rs_info.cc ../libRootSpy-client/rs_influxdb.cc)

# Link with libraries (assuming libRootSpy-client is built as a library)
# target_link_libraries(RSAI RootSpy-client ${ROOT_LIBRARIES} ${CMSG_LIBRARIES} ${xmsg_LIBRARIES} ${Protobuf_LIBRARIES} ${Protobuf_LITE_LIBRARIES})
target_link_libraries(RSAI ${ROOT_LIBRARIES} ${CMSG_LIBRARIES} ${xmsg_LIBRARIES} ${Protobuf_LIBRARIES} ${Protobuf_LITE_LIBRARIES})

message(STATUS "Protobuf_LIBRARIES=${Protobuf_LIBRARIES}")
# Function to print all CMake variables that contain "Protobuf"
function(print_protobuf_variables)
    message(STATUS "---- Protobuf-related CMake Variables ----")
    get_cmake_property(_variableNames VARIABLES)
    foreach (_variableName ${_variableNames})
        if (_variableName MATCHES "Protobuf")
            message(STATUS "${_variableName}=${${_variableName}}")
        endif()
    endforeach()
    message(STATUS "-----------------------------------------")
endfunction()

# Call the function to print Protobuf variables
print_protobuf_variables()