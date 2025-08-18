cmake_minimum_required(VERSION 3.31)

find_package(cmake-bare REQUIRED PATHS node_modules/cmake-bare)
find_package(cmake-fetch REQUIRED PATHS node_modules/cmake-fetch)

project(bare_gif C)

fetch_package("git:git.code.sf.net/p/giflib/code#5.2.2" SOURCE_DIR giflib)

add_library(giflib STATIC)

target_sources(
  giflib
  PRIVATE
    ${giflib}/dgif_lib.c
    ${giflib}/egif_lib.c
    ${giflib}/gif_err.c
    ${giflib}/gif_font.c
    ${giflib}/gif_hash.c
    ${giflib}/gif_hash.h
    ${giflib}/gif_lib.h
    ${giflib}/gif_lib_private.h
    ${giflib}/gifalloc.c
)

target_include_directories(
  giflib
  PUBLIC
    ${giflib}
)

add_bare_module(bare_gif)

target_sources(
  ${bare_gif}
  PRIVATE
    gif.h
    binding.c
)

target_link_libraries(
  ${bare_gif}
  PRIVATE
    giflib
)
