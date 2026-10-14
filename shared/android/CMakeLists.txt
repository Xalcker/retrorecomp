# La parte nativa de la APK (make apk, retrorecomp/shared/android): SDL2 desde su código fuente (libSDL2.so) y el
# proyecto generado en libmain.so, donde SDLActivity llama a SDL_main (el main de main_sdl.c). Del proyecto entran
# runtime/, gen/, platform/ (menos difftest y bench) y, si lo tiene, bios/ (NEORecomp); app.gradle pasa las rutas y
# los -I y -D del Makefile (RECOMP_INC), con los -I relativos al proyecto.
cmake_minimum_required(VERSION 3.10)
project(recomp C)

set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${SDL_DIR} SDL2)

file(GLOB RECOMP_SRC ${RECOMP_DIR}/runtime/*.c ${RECOMP_DIR}/gen/*.c ${RECOMP_DIR}/platform/*.c ${RECOMP_DIR}/bios/*.c)
list(FILTER RECOMP_SRC EXCLUDE REGEX "/platform/(difftest|bench)\\.c$")
add_library(main SHARED ${RECOMP_SRC} ${CMAKE_CURRENT_LIST_DIR}/android_jni.c)
set_target_properties(main PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)
# -O2 y -g0 van después del -O0 -g de la variante de depuración: el juego tiene que ir a velocidad, y con información
# de depuración el código generado (que puede ser enorme) deja a clang sin memoria. El escritorio tampoco la lleva
target_compile_options(main PRIVATE -O2 -g0 -Wall -Wno-unused-label)
separate_arguments(RECOMP_FLAGS UNIX_COMMAND "${RECOMP_INC}")
foreach(flag IN LISTS RECOMP_FLAGS)
    if(flag MATCHES "^-I(.+)$")
        get_filename_component(dir "${CMAKE_MATCH_1}" ABSOLUTE BASE_DIR "${RECOMP_DIR}")
        target_include_directories(main PRIVATE "${dir}")
    else()
        target_compile_options(main PRIVATE "${flag}")
    endif()
endforeach()
target_link_libraries(main SDL2 m log)
