# Per-game recompiled executables.
#
# Each game is a hand-written src/main.c (boot/loop glue) plus the
# v810recomp-generated generated/recomp_funcs.c, linked against the shared
# vbrecomp runtime library (defined in the parent CMakeLists).
#
# ROMs are not committed; pass the .vb path as argv[1] at run time, e.g.
#   ./galacticpinball "../../roms/Galactic Pinball (Japan, USA) (En).vb"

# vb_add_game(<target> <dir>) — register one game executable.
function(vb_add_game target dir)
    add_executable(${target}
        ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/src/main.c
        ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/generated/recomp_funcs.c
    )
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${dir})
    target_link_libraries(${target} PRIVATE vbrecomp)
    # Generated code is intentionally noisy; silence it.
    if(MSVC)
        target_compile_options(${target} PRIVATE /W1 /wd4244 /wd4146)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
endfunction()

# vb_add_game_generic(<target> <dir>) — register a game that uses the shared
# generic driver (games/driver/main.c) instead of a hand-written src/main.c.
# Works for any game whose reset path enters an infinite main loop.
function(vb_add_game_generic target dir)
    add_executable(${target}
        ${CMAKE_CURRENT_SOURCE_DIR}/driver/main.c
        ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/generated/recomp_funcs.c
    )
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/generated)
    target_link_libraries(${target} PRIVATE vbrecomp)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W1 /wd4244 /wd4146)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
endfunction()

# Hand-written boot glue (custom dispatch / reset returns):
vb_add_game(mariotennis     mariotennis)
vb_add_game(redalarm        redalarm)
vb_add_game(galacticpinball galacticpinball)

# Generic driver (no game-specific glue):
vb_add_game_generic(marioclash marioclash)
