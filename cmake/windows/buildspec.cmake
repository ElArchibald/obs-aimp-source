# CMake Windows build dependencies module

include_guard(GLOBAL)

include(buildspec_common)

# _check_dependencies_windows: Set up Windows slice for _check_dependencies
function(_check_dependencies_windows)
  if(NOT DEFINED OBS_STUDIO_DIR AND DEFINED ENV{OBS_STUDIO_DIR})
    set(OBS_STUDIO_DIR "$ENV{OBS_STUDIO_DIR}")
  elseif(NOT DEFINED OBS_STUDIO_DIR AND EXISTS "E:/Workspace/obs-sourcecode/obs-studio")
    set(OBS_STUDIO_DIR "E:/Workspace/obs-sourcecode/obs-studio" CACHE PATH "Path to obs-studio source root")
  endif()

  if(SKIP_OBS_DEPS OR OBS_STUDIO_DIR)
    message(STATUS "Using local OBS installation, skipping obs-deps download")
    return()
  endif()

  if(CMAKE_VS_PLATFORM_NAME)
    set(arch ${CMAKE_VS_PLATFORM_NAME})
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(arch "x64")
  else()
    set(arch "x86")
  endif()
  set(platform windows-${arch})

  set(dependencies_dir "${CMAKE_CURRENT_SOURCE_DIR}/.deps")
  set(prebuilt_filename "windows-deps-VERSION-ARCH-REVISION.zip")
  set(prebuilt_destination "obs-deps-VERSION-ARCH")
  set(qt6_filename "windows-deps-qt6-VERSION-ARCH-REVISION.zip")
  set(qt6_destination "obs-deps-qt6-VERSION-ARCH")
  set(obs-studio_filename "VERSION.zip")
  set(obs-studio_destination "obs-studio-VERSION")
  set(dependencies_list prebuilt qt6 obs-studio)

  _check_dependencies()
endfunction()

_check_dependencies_windows()
