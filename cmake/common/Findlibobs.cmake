# Fallback Findlibobs module for local OBS source tree / libraries

include_guard(GLOBAL)

if(TARGET OBS::libobs)
  set(libobs_FOUND TRUE)
  return()
endif()

if(NOT DEFINED OBS_STUDIO_DIR AND DEFINED ENV{OBS_STUDIO_DIR})
  set(OBS_STUDIO_DIR "$ENV{OBS_STUDIO_DIR}")
elseif(NOT DEFINED OBS_STUDIO_DIR)
  set(OBS_STUDIO_DIR "E:/Workspace/obs-sourcecode/obs-studio" CACHE PATH "Path to obs-studio source root")
endif()

set(OBS_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/obs-libs")

find_path(
  LIBOBS_INCLUDE_DIR
  NAMES obs.h
  PATHS
    "${OBS_STUDIO_DIR}/libobs"
    "${OBS_STUDIO_DIR}/include"
)

find_library(
  LIBOBS_LIB
  NAMES obs libobs
  PATHS
    "${OBS_LIB_DIR}"
    "${OBS_STUDIO_DIR}/build/libobs"
    "${OBS_STUDIO_DIR}/build_x64/libobs"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  libobs
  DEFAULT_MSG
  LIBOBS_INCLUDE_DIR
  LIBOBS_LIB
)

if(libobs_FOUND AND NOT TARGET OBS::libobs)
  add_library(OBS::libobs UNKNOWN IMPORTED)
  set_target_properties(
    OBS::libobs
    PROPERTIES
      IMPORTED_LOCATION "${LIBOBS_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${LIBOBS_INCLUDE_DIR}"
  )
endif()

