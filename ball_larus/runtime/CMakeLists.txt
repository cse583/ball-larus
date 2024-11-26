add_library(BallLarusRuntime SHARED
  runtime.cpp
)

target_compile_features(BallLarusRuntime PRIVATE cxx_std_17)

# Set output directory
set_target_properties(BallLarusRuntime PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)