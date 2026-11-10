set(OPENSPACE_DEPENDENCIES
  globebrowsing
  video
)

# The stream layer plays its segments through the video module
if (WIN32 AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
  set(SUPPORTED OFF)
endif ()
