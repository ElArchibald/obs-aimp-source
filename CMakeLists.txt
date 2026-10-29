cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  if(NOT DEFINED QT6_PREFIX_PATH AND DEFINED ENV{QT6_PREFIX_PATH})
    set(QT6_PREFIX_PATH "$ENV{QT6_PREFIX_PATH}")
  elseif(NOT DEFINED QT6_PREFIX_PATH AND EXISTS "C:/Users/Alex/AppData/Local/runt-nightly/envs/runtimed-pixi-81de3cdf-8786-4bdb-80d9-fb762666e5f0/.pixi/envs/default/Library")
    set(QT6_PREFIX_PATH "C:/Users/Alex/AppData/Local/runt-nightly/envs/runtimed-pixi-81de3cdf-8786-4bdb-80d9-fb762666e5f0/.pixi/envs/default/Library" CACHE PATH "Path to Qt6 root directory")
  endif()
  if(QT6_PREFIX_PATH)
    list(PREPEND CMAKE_PREFIX_PATH "${QT6_PREFIX_PATH}")
    cmake_path(CONVERT "${QT6_PREFIX_PATH}/bin" TO_NATIVE_PATH_LIST QT6_BIN_DIR_NATIVE)
    set(ENV{PATH} "${QT6_BIN_DIR_NATIVE};$ENV{PATH}")
  endif()

  find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF
  )
endif()

# AIMP SDK Path configuration
if(NOT DEFINED AIMP_SDK_DIR AND DEFINED ENV{AIMP_SDK_DIR})
  set(AIMP_SDK_DIR "$ENV{AIMP_SDK_DIR}")
elseif(NOT DEFINED AIMP_SDK_DIR)
  set(AIMP_SDK_DIR "E:/Workspace/aimp_sdk/Sources/Cpp" CACHE PATH "Path to AIMP SDK C++ sources")
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/aimp-controller.hpp
    src/aimp-controller.cpp
    src/aimp-audio.hpp
    src/aimp-audio.cpp
    src/aimp-overlay-theme.hpp
    src/aimp-overlay-theme.cpp
    src/aimp-overlay.hpp
    src/aimp-overlay.cpp
    src/aimp-dock.hpp
    src/aimp-dock.cpp
    src/aimp-source.hpp
    src/aimp-source.cpp
)

target_include_directories(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${AIMP_SDK_DIR}"
)

target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)

target_compile_definitions(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      /W4
      /wd4100 # unreferenced formal parameter
      /wd4201 # nameless struct/union
  )
endif()

if(OS_WINDOWS)
  target_link_libraries(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      shlwapi
      ole32
      oleaut32
      shell32
      user32
      gdi32
      advapi32
  )
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
