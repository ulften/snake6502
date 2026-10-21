cmake_minimum_required(VERSION 3.21)

include(${CMAKE_CURRENT_LIST_DIR}/tools/rp6502.cmake)

project(SNAKE C CXX ASM)
add_executable(snake)
rp6502_map(snake src/xram.h "XRAM_.*")
rp6502_asset(snake help src/help.txt)
rp6502_executable(snake DATA default RESET default)
rp6502_web(snake)
target_sources(snake PRIVATE
     src/xram.h
     src/main.c
     src/gfx.c
     src/grass.c
     src/hud.c
     src/sound.c
     src/input.c
     src/hiscore.c
     src/log.c
)
