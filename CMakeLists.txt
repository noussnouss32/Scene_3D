cmake_minimum_required(VERSION 3.14)
project(EnchantedForest LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)

# ---- GLFW : version système si présente, sinon téléchargée ----
find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
  message(STATUS "GLFW introuvable sur le système : téléchargement via FetchContent...")
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glfw)
endif()

# ---- GLM : copie locale (external/glm) > version système > téléchargement ----
set(GLM_LOCAL "${CMAKE_CURRENT_SOURCE_DIR}/external/glm")
if(EXISTS "${GLM_LOCAL}/glm/glm.hpp")
  message(STATUS "GLM : copie locale (external/glm)")
  add_library(glm_local INTERFACE)
  target_include_directories(glm_local INTERFACE "${GLM_LOCAL}")
  set(GLM_TARGET glm_local)
else()
  find_package(glm QUIET)
  if(glm_FOUND)
    message(STATUS "GLM : version système")
    set(GLM_TARGET glm::glm)
  else()
    message(STATUS "GLM introuvable : téléchargement via FetchContent...")
    FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.1 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glm)
    set(GLM_TARGET glm::glm)
  endif()
endif()

# ---- Exécutable (glad est fourni dans external/glad) ----
add_executable(enchanted_forest
  src/main.cpp
  external/glad/src/gl.c)

target_include_directories(enchanted_forest PRIVATE
  src
  external/glad/include)

target_link_libraries(enchanted_forest PRIVATE glfw ${GLM_TARGET} ${CMAKE_DL_LIBS})

if(MSVC)
  target_compile_options(enchanted_forest PRIVATE /utf-8 /W3)
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT enchanted_forest)
else()
  target_compile_options(enchanted_forest PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
