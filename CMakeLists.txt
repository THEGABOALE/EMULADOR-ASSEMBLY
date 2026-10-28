cmake_minimum_required(VERSION 3.16)
project(EmuladorX86 VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(EMU_WITH_UNICORN  "Backend de código máquina real con Unicorn Engine" ON)
option(EMU_WITH_CAPSTONE "Desensamblador Capstone" ON)
option(EMU_WITH_KEYSTONE "Ensamblador Keystone" ON)

# ---------------------------------------------------------------- dependencias
# Se buscan en las rutas estándar del compilador (p. ej. C:/msys64/ucrt64 o
# /usr). Use -DCMAKE_PREFIX_PATH=<ruta> si están instaladas en otro lugar.
function(emu_find_dep NAME HEADER LIBNAMES)
  find_path(${NAME}_INCLUDE_DIR ${HEADER})
  find_library(${NAME}_LIBRARY NAMES ${LIBNAMES})
  if(${NAME}_INCLUDE_DIR AND ${NAME}_LIBRARY)
    set(${NAME}_FOUND TRUE PARENT_SCOPE)
    message(STATUS "${NAME}: ${${NAME}_LIBRARY}")
  else()
    set(${NAME}_FOUND FALSE PARENT_SCOPE)
    message(WARNING "${NAME} no encontrado: el modo correspondiente quedará desactivado")
  endif()
endfunction()

set(EMU_DEP_DEFS "")
set(EMU_DEP_LIBS "")
set(EMU_DEP_INCS "")
foreach(dep UNICORN CAPSTONE KEYSTONE)
  if(EMU_WITH_${dep})
    string(TOLOWER ${dep} lname)
    emu_find_dep(${dep} "${lname}/${lname}.h" "${lname}")
    if(${dep}_FOUND)
      list(APPEND EMU_DEP_DEFS HAVE_${dep})
      list(APPEND EMU_DEP_LIBS ${${dep}_LIBRARY})
      list(APPEND EMU_DEP_INCS ${${dep}_INCLUDE_DIR})
    endif()
  endif()
endforeach()

# ---------------------------------------------------------------- objetivos
add_library(emu_core STATIC
  src/cpu.cpp
  src/parser.cpp
  src/interpreter.cpp
  src/trace.cpp
  src/machine.cpp
  src/hexutil.cpp
  src/examples.cpp
  src/cli.cpp)
target_include_directories(emu_core PUBLIC src ${EMU_DEP_INCS})
target_compile_definitions(emu_core PUBLIC ${EMU_DEP_DEFS})
target_link_libraries(emu_core PUBLIC ${EMU_DEP_LIBS})
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(emu_core PRIVATE -Wall -Wextra)
endif()

add_executable(emulador src/main.cpp)
target_link_libraries(emulador PRIVATE emu_core)
if(WIN32)
  target_link_libraries(emulador PRIVATE shell32)  # CommandLineToArgvW
endif()

if(MINGW)
  # Evita depender de libstdc++/libgcc del compilador para nuestro código.
  target_link_options(emulador PRIVATE -static-libgcc -static-libstdc++)
endif()

# En Windows se copian junto al .exe las DLL de las bibliotecas (y sus
# dependencias de tiempo de ejecución) para poder ejecutarlo sin configurar PATH.
if(WIN32)
  get_filename_component(EMU_COMPILER_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
  set(EMU_RUNTIME_DLLS "")
  foreach(dll libunicorn.dll libcapstone.dll libkeystone.dll
              libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
    if(EXISTS "${EMU_COMPILER_BIN}/${dll}")
      list(APPEND EMU_RUNTIME_DLLS "${EMU_COMPILER_BIN}/${dll}")
    endif()
  endforeach()
  if(EMU_RUNTIME_DLLS)
    add_custom_command(TARGET emulador POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EMU_RUNTIME_DLLS} $<TARGET_FILE_DIR:emulador>
      COMMENT "Copiando DLL de Unicorn/Capstone/Keystone junto al ejecutable")
  endif()
endif()
