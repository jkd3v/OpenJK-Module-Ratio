cmake_minimum_required (VERSION 2.6)
project (ratio)

# add preprocessor definitions
add_definitions(-D_CGAME -DNO_CG_TRAP)

# add include directories
include_directories(include/sdk include/mpp)

# add the executable
file(GLOB module_SRC
  "src/*.h"
  "src/*.c"
)
add_library(ratio SHARED ${module_SRC})