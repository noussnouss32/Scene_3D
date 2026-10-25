cmake_minimum_required(VERSION 3.14)
project(EnchantedForest LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---- GLFW : utilise la version système si présente, sinon la télécharge ----
find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
  message(STATUS "GLFW introuvable sur le système : téléchargement via FetchContent...")
  include(FetchContent)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glfw)
endif()

# ---- Exécutable (glad + GLM sont fournis dans external/) ----
add_executable(enchanted_forest
  src/main.cpp
  external/glad/src/gl.c)

target_include_directories(enchanted_forest PRIVATE
  src
  external/glad/include
  external/glm)

target_link_libraries(enchanted_forest PRIVATE glfw ${CMAKE_DL_LIBS})

if(MSVC)
  target_compile_options(enchanted_forest PRIVATE /utf-8 /W3)
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT enchanted_forest)
else()
  target_compile_options(enchanted_forest PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
