cmake_minimum_required(VERSION 3.24)
project(scooby LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# The sound cores are git submodules. Fetch them if the clone was not recursive.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/z80/z80.c" OR NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_opn.cpp")
  find_package(Git REQUIRED)
  message(STATUS "Fetching submodules")
  execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init
                  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" COMMAND_ERROR_IS_FATAL ANY)
endif()

# On Windows link the C++ runtime statically, so the program runs on a
# machine without the Visual C++ redistributable.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set(SFML_USE_STATIC_STD_LIBS ON CACHE BOOL "" FORCE)
endif()

# Use an installed SFML 3 if there is one, otherwise download and build it.
find_package(SFML 3 QUIET COMPONENTS Graphics Window Audio System)
if(NOT SFML_FOUND)
  message(STATUS "SFML 3 not found on this system; downloading it")
  include(FetchContent)
  set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 3.0.2
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
    SYSTEM)
  FetchContent_MakeAvailable(SFML)
endif()

if(MSVC)
  set(QUIET_FLAGS /w)
  set(WARN_FLAGS /W3)
else()
  set(QUIET_FLAGS -w)
  set(WARN_FLAGS -Wall -Wextra)
endif()

# Sound chip and CPU cores (MIT and BSD-3-Clause, see third_party/*/LICENSE).
add_library(soundcores STATIC third_party/z80/z80.c third_party/ymfm/src/ymfm_opn.cpp
  third_party/ymfm/src/ymfm_ssg.cpp third_party/ymfm/src/ymfm_adpcm.cpp)
target_include_directories(soundcores SYSTEM PUBLIC third_party/z80 third_party/ymfm/src)
target_compile_options(soundcores PRIVATE ${QUIET_FLAGS})

file(GLOB CORE_SRC CONFIGURE_DEPENDS src/core/*.cpp)
file(GLOB GAME_SRC CONFIGURE_DEPENDS src/game/*.cpp)

add_library(engine STATIC ${CORE_SRC} ${GAME_SRC})
target_include_directories(engine PUBLIC src)
target_link_libraries(engine PUBLIC soundcores)
target_compile_options(engine PRIVATE ${WARN_FLAGS})

add_executable(scooby src/main.cpp)
target_link_libraries(scooby PRIVATE engine SFML::Graphics SFML::Window SFML::Audio SFML::System)
target_compile_options(scooby PRIVATE ${WARN_FLAGS})

# Put the program at the top of the build directory on every generator.
set_target_properties(scooby PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}" RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}")

add_executable(vdptest tests/vdptest.cpp src/core/vdp.cpp)
