# Fallback Findobs-frontend-api module for local OBS source tree / libraries

include_guard(GLOBAL)

if(TARGET OBS::obs-frontend-api)
  set(obs-frontend-api_FOUND TRUE)
  return()
endif()

if(NOT DEFINED OBS_STUDIO_DIR AND DEFINED ENV{OBS_STUDIO_DIR})
  set(OBS_STUDIO_DIR "$ENV{OBS_STUDIO_DIR}")
elseif(NOT DEFINED OBS_STUDIO_DIR)
  set(OBS_STUDIO_DIR "E:/Workspace/obs-sourcecode/obs-studio" CACHE PATH "Path to obs-studio source root")
endif()

set(OBS_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/obs-libs")

find_path(
  OBS_FRONTEND_API_INCLUDE_DIR
  NAMES obs-frontend-api.h
  PATHS
    "${OBS_STUDIO_DIR}/frontend/api"
    "${OBS_STUDIO_DIR}/include"
)

find_library(
  OBS_FRONTEND_API_LIB
  NAMES obs-frontend-api libobs-frontend-api
  PATHS
    "${OBS_LIB_DIR}"
    "${OBS_STUDIO_DIR}/build/frontend/api"
    "${OBS_STUDIO_DIR}/build_x64/frontend/api"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  obs-frontend-api
  DEFAULT_MSG
  OBS_FRONTEND_API_INCLUDE_DIR
  OBS_FRONTEND_API_LIB
)

if(obs-frontend-api_FOUND AND NOT TARGET OBS::obs-frontend-api)
  add_library(OBS::obs-frontend-api UNKNOWN IMPORTED)
  set_target_properties(
    OBS::obs-frontend-api
    PROPERTIES
      IMPORTED_LOCATION "${OBS_FRONTEND_API_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${OBS_FRONTEND_API_INCLUDE_DIR}"
  )
endif()

