cmake_minimum_required(VERSION 3.24)
project(scooby LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(SFML 3 REQUIRED COMPONENTS Graphics Window Audio System)

# Sound chip and CPU cores (MIT and BSD-3-Clause, see third_party/*/LICENSE).
add_library(soundcores STATIC third_party/z80/z80.c third_party/ymfm/src/ymfm_opn.cpp
  third_party/ymfm/src/ymfm_ssg.cpp third_party/ymfm/src/ymfm_adpcm.cpp)
target_include_directories(soundcores SYSTEM PUBLIC third_party/z80 third_party/ymfm/src)
target_compile_options(soundcores PRIVATE -w)

file(GLOB CORE_SRC CONFIGURE_DEPENDS src/core/*.cpp)
file(GLOB GAME_SRC CONFIGURE_DEPENDS src/game/*.cpp)

add_library(engine STATIC ${CORE_SRC} ${GAME_SRC})
target_include_directories(engine PUBLIC src)
target_link_libraries(engine PUBLIC soundcores)
target_compile_options(engine PRIVATE -Wall -Wextra)

add_executable(scooby src/main.cpp)
target_link_libraries(scooby PRIVATE engine SFML::Graphics SFML::Window SFML::Audio SFML::System)
target_compile_options(scooby PRIVATE -Wall -Wextra)

add_executable(vdptest tests/vdptest.cpp src/core/vdp.cpp)
