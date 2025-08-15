cmake_minimum_required(VERSION 3.31)

find_package(cmake-bare REQUIRED PATHS node_modules/cmake-bare)
find_package(cmake-fetch REQUIRED PATHS node_modules/cmake-fetch)

project(bare_gif C)

fetch_package("github:nothings/stb#802cd45" SOURCE_DIR stb)

add_bare_module(bare_gif)

target_sources(
  ${bare_gif}
  PRIVATE
    binding.c
)

target_include_directories(
  ${bare_gif}
  PRIVATE
    ${stb}
)
