cmake_minimum_required(VERSION 3.31)

find_package(cmake-bare REQUIRED PATHS node_modules/cmake-bare)
find_package(cmake-fetch REQUIRED PATHS node_modules/cmake-fetch)

project(bare_gif C)

fetch_package("git:git.code.sf.net/p/giflib/code#5.0.5" SOURCE_DIR giflib)

add_library(giflib STATIC)

target_sources(
  giflib
  PRIVATE
    ${giflib}/lib/dgif_lib.c
    ${giflib}/lib/egif_lib.c
    ${giflib}/lib/gif_err.c
    ${giflib}/lib/gif_font.c
    ${giflib}/lib/gif_hash.c
    ${giflib}/lib/gif_hash.h
    ${giflib}/lib/gif_lib.h
    ${giflib}/lib/gif_lib_private.h
    ${giflib}/lib/gifalloc.c
)

target_include_directories(
  giflib
  PUBLIC
    ${giflib}/lib
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
