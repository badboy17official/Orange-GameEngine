cmake_minimum_required(VERSION 3.10)

project(third-person-shooter-engine)

set(CMAKE_CXX_STANDARD 17)

# Add engine and game source files
file(GLOB_RECURSE ENGINE_SRC "engine/**/*.cpp")
file(GLOB_RECURSE GAME_SRC "game/**/*.cpp")

add_executable(third_person_shooter
    main.cpp
    ${ENGINE_SRC}
    ${GAME_SRC}
)

target_include_directories(third_person_shooter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/core
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/graphics
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/input
    ${CMAKE_CURRENT_SOURCE_DIR}/game
)