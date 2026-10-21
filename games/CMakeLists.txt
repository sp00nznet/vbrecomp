# Recompiled code needs a far bigger stack than the V810 original did: one
# guest function becomes one C function, the transfer chain nests real C
# frames, and those frames are much larger than the 8-16 bytes the guest
# pushed. 3-D Tetris and Null Pointer Mockup both died with
# STATUS_STACK_OVERFLOW (0xC00000FD) on the default 1 MB while their logical
# call depth stayed under 40 -- it is frame SIZE, not runaway recursion. Both
# run to completion with this. On x64 the value is reserved address space and
# is committed only as it is touched, so it costs nothing until used.
function(vb_game_setup target)
    if(MSVC)
        target_link_options(${target} PRIVATE "/STACK:1073741824")
    endif()
    # Put SDL2.dll beside the exe, so it runs from where it was built. vcpkg's
    # MSBuild applocal step needs pwsh (PowerShell 7) and fails without it; it
    # was also seen leaving the output folder with no SDL2.dll at all.
    if(TARGET SDL2::SDL2)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:${target}>)
    endif()
endfunction()

# Per-game recompiled executables.
#
# Each game is a hand-written src/main.c (boot/loop glue) plus the
# v810recomp-generated generated/recomp_funcs.c, linked against the shared
# vbrecomp runtime library (defined in the parent CMakeLists).
#
# ROMs are not committed; pass the .vb path as argv[1] at run time, e.g.
#   ./galacticpinball "../../roms/Galactic Pinball (Japan, USA) (En).vb"

# NOTE: generated/recomp_funcs.c is NOT committed. It is a machine translation
# of a copyrighted ROM -- a derived work of that ROM, not of this project, and
# not ours to redistribute. Regenerate it before building these targets:
#
#   build/Release/v810recomp.exe "roms/<Game>.vb" games/<dir>/generated
#
# Each target below is skipped when its generated file is absent, so a fresh
# clone configures and builds the library and tools without it.
# vb_add_game(<target> <dir>) — register one game executable.
function(vb_add_game target dir)
    set(gen ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/generated/recomp_funcs.c)
    if(NOT EXISTS ${gen})
        message(STATUS "skipping ${target}: ${gen} not generated yet")
        return()
    endif()
    add_executable(${target}
        ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/src/main.c
        ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/generated/recomp_funcs.c
    )
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${dir})
    target_link_libraries(${target} PRIVATE vbrecomp)
    vb_game_setup(${target})
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
    set(gen ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/generated/recomp_funcs.c)
    if(NOT EXISTS ${gen})
        message(STATUS "skipping ${target}: ${gen} not generated yet")
        return()
    endif()
    add_executable(${target}
        ${CMAKE_CURRENT_SOURCE_DIR}/driver/main.c
        ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/generated/recomp_funcs.c
    )
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/generated)
    target_link_libraries(${target} PRIVATE vbrecomp)
    vb_game_setup(${target})
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
vb_add_game_generic(golf       golf)

# Build EVERY recompiled ROM in a corpus dir via the generic driver. Used by
# scripts/build_all.ps1: configure with -DVBRECOMP_CORPUS_DIR=<abs corpus path>.
# Each subdir with a recomp_funcs.c becomes a `corpus_<slug>` executable.
if(DEFINED VBRECOMP_CORPUS_DIR)
    file(GLOB corpus_entries RELATIVE ${VBRECOMP_CORPUS_DIR} ${VBRECOMP_CORPUS_DIR}/*)
    foreach(slug ${corpus_entries})
        set(gen ${VBRECOMP_CORPUS_DIR}/${slug}/recomp_funcs.c)
        if(EXISTS ${gen})
            add_executable(corpus_${slug}
                ${CMAKE_CURRENT_SOURCE_DIR}/driver/main.c ${gen})
            target_include_directories(corpus_${slug} PRIVATE ${VBRECOMP_CORPUS_DIR}/${slug})
            target_link_libraries(corpus_${slug} PRIVATE vbrecomp)
            vb_game_setup(corpus_${slug})
            if(MSVC)
                # /Od: these generated files are huge and only boot-tested,
                # so skip optimization to keep the 76-target build tractable.
                target_compile_options(corpus_${slug} PRIVATE /W0 /Od /wd4244 /wd4146)
            else()
                target_compile_options(corpus_${slug} PRIVATE -O0 -w)
            endif()
        endif()
    endforeach()
endif()
