project(libRootSpy)

# Define the library target
add_library(RootSpy DRootSpy.cc)

# Assuming you have a target named 'your_target_name'
target_compile_options(RootSpy PRIVATE -fpermissive)
