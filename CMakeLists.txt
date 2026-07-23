cmake_minimum_required(VERSION 3.5)

file(STRINGS src/game/version.h VERSION_LINE
  LIMIT_COUNT 1
  REGEX "^#define GAME_VERSION "
)

if(VERSION_LINE MATCHES "\"([0-9]+)\\.([0-9]+)\\.([0-9]+)\"")
  set(VERSION_MAJOR ${CMAKE_MATCH_1})
  set(VERSION_MINOR ${CMAKE_MATCH_2})
  set(VERSION_PATCH ${CMAKE_MATCH_3})
elseif(VERSION_LINE MATCHES "\"([0-9]+)\\.([0-9]+)\"")
  set(VERSION_MAJOR ${CMAKE_MATCH_1})
  set(VERSION_MINOR ${CMAKE_MATCH_2})
  set(VERSION_PATCH "0")
else()
  message(FATAL_ERROR "Couldn't parse version from src/game/version.h")
endif()

project(openbattle VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

set(ORIGINAL_CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH})
set(OWN_CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)
set(CMAKE_MODULE_PATH ${OWN_CMAKE_MODULE_PATH})

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(TARGET_BITS "64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(TARGET_BITS "32")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(TARGET_OS "windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(TARGET_OS "linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(TARGET_OS "mac")
endif()

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(CheckSymbolExists)

check_symbol_exists(__i386 "" TARGET_ARCH_X86_i386)
if(TARGET_ARCH_X86_i386)
  set(TARGET_ARCH x86)
else()
  set(TARGET_ARCH)
endif()

set(AUTO_DEPENDENCIES_DEFAULT OFF)
if(TARGET_OS STREQUAL "windows")
  set(AUTO_DEPENDENCIES_DEFAULT ON)
endif()

option(PREFER_BUNDLED_LIBS "Prefer bundled libraries over system libraries" ${AUTO_DEPENDENCIES_DEFAULT})
option(DEV "Don't generate stuff necessary for packaging" OFF)
option(TOOLS "Compile development tools" OFF)

if(NOT CMAKE_BUILD_TYPE)
  if(DEV)
    set(CMAKE_BUILD_TYPE Release)
  else()
    set(CMAKE_BUILD_TYPE Debug)
  endif()
endif()

set(DBG $<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  src/game/version.h
)

set(SERVER_EXECUTABLE teeworlds_srv CACHE STRING "Name of the built server executable")

########################################################################
# Compiler flags
########################################################################

function(add_c_compiler_flag_if_supported VARIABLE FLAG)
  if(ARGC GREATER 2)
    set(CHECKED_FLAG "${ARGV2}")
  else()
    set(CHECKED_FLAG "${FLAG}")
  endif()
  string(REGEX REPLACE "[^A-Za-z0-9]" "_" CONFIG_VARIABLE "FLAG_SUPPORTED${CHECKED_FLAG}")
  check_c_compiler_flag("${CHECKED_FLAG}" ${CONFIG_VARIABLE})
  if(${CONFIG_VARIABLE})
    if(${VARIABLE})
      set("${VARIABLE}" "${${VARIABLE}};${FLAG}" PARENT_SCOPE)
    else()
      set("${VARIABLE}" "${FLAG}" PARENT_SCOPE)
    endif()
  endif()
endfunction()

if(NOT MSVC)
  add_c_compiler_flag_if_supported(OUR_FLAGS -fstack-protector-all)

  if(TARGET_ARCH STREQUAL "x86")
    add_c_compiler_flag_if_supported(OUR_FLAGS -ffloat-store)
  endif()

  if(TARGET_OS STREQUAL "mac")
    add_c_compiler_flag_if_supported(OUR_FLAGS -stdlib=libc++)
    add_c_compiler_flag_if_supported(OUR_FLAGS -mmacosx-version-min=10.7)
  endif()

  add_c_compiler_flag_if_supported(OUR_FLAGS_OWN -Wall)
  add_c_compiler_flag_if_supported(OUR_FLAGS_OWN -Wextra)
  add_c_compiler_flag_if_supported(OUR_FLAGS_OWN -Wno-unused-parameter)
  add_c_compiler_flag_if_supported(OUR_FLAGS_OWN -Wno-missing-field-initializers)
  add_c_compiler_flag_if_supported(OUR_FLAGS_OWN -Wformat=2)
  add_c_compiler_flag_if_supported(OUR_FLAGS_DEP -Wno-implicit-function-declaration)
endif()

if(NOT MSVC)
  check_c_compiler_flag("-O2;-Wp,-Werror;-D_FORTIFY_SOURCE=2" DEFINE_FORTIFY_SOURCE)
endif()

########################################################################
# COMMON FUNCTIONS
########################################################################

function(set_glob VAR GLOBBING DIRECTORY) # ...
  file(${GLOBBING} GLOB_RESULT "${DIRECTORY}/*.c" "${DIRECTORY}/*.cpp" "${DIRECTORY}/*.h")
  list(SORT GLOB_RESULT)
  set(FILES)
  foreach(file ${ARGN})
    list(APPEND FILES "${PROJECT_SOURCE_DIR}/${DIRECTORY}/${file}")
  endforeach()

  if(NOT FILES STREQUAL GLOB_RESULT)
    message(AUTHOR_WARNING "${VAR} does not contain every file from directory ${DIRECTORY}")
    if(POLICY CMP0057)
      cmake_policy(SET CMP0057 NEW)
      set(GLOB_BUT_NOT_LIST)
      foreach(file ${GLOB_RESULT})
        if(NOT file IN_LIST FILES)
          list(APPEND GLOB_BUT_NOT_LIST ${file})
        endif()
      endforeach()
      if(GLOB_BUT_NOT_LIST)
        message(AUTHOR_WARNING "Entries only present in ${DIRECTORY}: ${GLOB_BUT_NOT_LIST}")
      endif()
    endif()
  endif()

  set(${VAR} ${FILES} PARENT_SCOPE)
endfunction()

########################################################################
# INITIALIZE TARGET LISTS
########################################################################

set(TARGETS_OWN)
set(TARGETS_DEP)
set(TARGETS_LINK)

########################################################################
# DEPENDENCIES
########################################################################

function(set_extra_dirs_lib VARIABLE NAME)
  set("PATHS_${VARIABLE}_LIBDIR" PARENT_SCOPE)
  set("HINTS_${VARIABLE}_LIBDIR" PARENT_SCOPE)
  if(PREFER_BUNDLED_LIBS)
    set(TYPE HINTS)
  else()
    set(TYPE PATHS)
  endif()
  if(TARGET_BITS AND TARGET_OS)
    set(DIR "other/${NAME}/${TARGET_OS}/lib${TARGET_BITS}")
    set("${TYPE}_${VARIABLE}_LIBDIR" "${DIR}" PARENT_SCOPE)
    set("EXTRA_${VARIABLE}_LIBDIR" "${DIR}" PARENT_SCOPE)
  endif()
endfunction()

function(set_extra_dirs_include VARIABLE NAME LIBRARY)
  set("PATHS_${VARIABLE}_INCLUDEDIR" PARENT_SCOPE)
  set("HINTS_${VARIABLE}_INCLUDEDIR" PARENT_SCOPE)
  is_bundled(IS_BUNDLED "${LIBRARY}")
  if(IS_BUNDLED)
    set("HINTS_${VARIABLE}_INCLUDEDIR" "other/${NAME}/include" "other/${NAME}/include/${TARGET_OS}" PARENT_SCOPE)
  endif()
endfunction()

if(CMAKE_CROSSCOMPILING)
  set(CROSSCOMPILING_NO_CMAKE_SYSTEM_PATH NO_CMAKE_SYSTEM_PATH)
else()
  set(CROSSCOMPILING_NO_CMAKE_SYSTEM_PATH)
endif()

function(is_bundled VARIABLE PATH)
  if(PATH)
    string(FIND "${PATH}" "${PROJECT_SOURCE_DIR}" LOCAL_PATH_POS)
    if(LOCAL_PATH_POS EQUAL 0 AND TARGET_BITS AND TARGET_OS)
      set("${VARIABLE}" ON PARENT_SCOPE)
    else()
      set("${VARIABLE}" OFF PARENT_SCOPE)
    endif()
  else()
    set("${VARIABLE}" OFF PARENT_SCOPE)
  endif()
endfunction()

if(NOT CMAKE_CROSSCOMPILING)
  find_package(PkgConfig QUIET)
endif()
find_package(ZLIB)
find_package(Git)
find_package(PythonInterp)
find_package(Threads)
find_package(CURL)
find_package(OpenSSL)
if(TOOLS)
  find_package(Pnglite)
endif()

message(STATUS "******** OpenBattle (server) ********")
message(STATUS "Target OS: ${TARGET_OS} ${TARGET_BITS}bit")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

message(STATUS "Dependencies:")
function(show_dependency_status OUTPUT_NAME NAME)
  if(${NAME}_FOUND)
    if(${NAME}_BUNDLED)
      message(STATUS " * ${OUTPUT_NAME} not found (using bundled version)")
    else()
      message(STATUS " * ${OUTPUT_NAME} found")
    endif()
  else()
    message(STATUS " * ${OUTPUT_NAME} not found")
  endif()
endfunction()

show_dependency_status("PythonInterp" PYTHONINTERP)
show_dependency_status("Zlib" ZLIB)
show_dependency_status("Curl" CURL)
show_dependency_status("OpenSSL" OPENSSL)
if(TOOLS)
  show_dependency_status("Pnglite" PNGLITE)
endif()

if(NOT PYTHONINTERP_FOUND)
  message(SEND_ERROR "You must install Python to compile OpenBattle")
endif()
if(NOT CURL_FOUND)
  message(SEND_ERROR "You must install libcurl to compile OpenBattle (DDNet HTTP register)")
endif()
if(NOT OPENSSL_FOUND)
  message(SEND_ERROR "You must install OpenSSL to compile OpenBattle (SHA256 / HTTPS)")
endif()

if(TARGET_OS STREQUAL "windows")
  set(PLATFORM_LIBS ws2_32)
elseif(TARGET_OS STREQUAL "mac")
  find_library(CARBON Carbon)
  find_library(COCOA Cocoa)
  set(PLATFORM_LIBS ${CARBON})
else()
  if(TARGET_OS STREQUAL "linux")
    set(PLATFORM_LIBS rt)
  else()
    set(PLATFORM_LIBS)
  endif()
endif()

########################################################################
# COPY DATA
########################################################################

if(EXISTS "${PROJECT_SOURCE_DIR}/data")
  file(COPY data DESTINATION .)
endif()

########################################################################
# CODE GENERATION
########################################################################

function(chash output_file)
  add_custom_command(OUTPUT ${output_file}
    COMMAND ${PYTHON_EXECUTABLE} scripts/cmd5.py ${ARGN}
      > "${PROJECT_BINARY_DIR}/${output_file}"
    DEPENDS scripts/cmd5.py ${ARGN}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  )
endfunction()

function(generate_source output_file script_parameter)
  add_custom_command(OUTPUT ${output_file}
    COMMAND ${PYTHON_EXECUTABLE} datasrc/compile.py ${script_parameter}
      > "${PROJECT_BINARY_DIR}/${output_file}"
    DEPENDS
      datasrc/compile.py
      datasrc/content.py
      datasrc/datatypes.py
      datasrc/network.py
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  )
endfunction()

function(generate_source7 output_file script_parameter)
  add_custom_command(OUTPUT ${output_file}
    COMMAND ${PYTHON_EXECUTABLE} -m datasrc.seven.compile ${script_parameter}
      > "${PROJECT_BINARY_DIR}/${output_file}"
    DEPENDS
      datasrc/seven/compile.py
      datasrc/seven/content.py
      datasrc/seven/datatypes.py
      datasrc/seven/network.py
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  )
endfunction()

function(generate_maps output_file)
  add_custom_command(OUTPUT ${output_file}
    COMMAND ${PYTHON_EXECUTABLE} datasrc/crosscompile.py
      > "${PROJECT_BINARY_DIR}/${output_file}"
    DEPENDS
      datasrc/compile.py
      datasrc/content.py
      datasrc/datatypes.py
      datasrc/network.py
      datasrc/crosscompile.py
      datasrc/seven/compile.py
      datasrc/seven/content.py
      datasrc/seven/datatypes.py
      datasrc/seven/network.py
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  )
endfunction()

file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/src/game/generated/")

chash("src/game/generated/nethash.cpp"
  "src/engine/shared/protocol.h"
  "${PROJECT_BINARY_DIR}/src/game/generated/protocol.h"
  "src/game/tuning.h"
  "src/game/gamecore.cpp"
)
generate_source("src/game/generated/protocol.cpp" "network_source")
generate_source("src/game/generated/protocol.h" "network_header")
generate_source("src/game/generated/server_data.cpp" "server_content_source")
generate_source("src/game/generated/server_data.h" "server_content_header")
generate_source7("src/game/generated/protocol7.cpp" "network_source")
generate_source7("src/game/generated/protocol7.h" "network_header")
generate_maps("src/game/generated/protocolglue.h")

########################################################################
# SHARED
########################################################################

set_glob(BASE GLOB_RECURSE src/base
  detect.h
  hash.c
  hash.h
  hash_ctxt.h
  hash_openssl.c
  math.h
  system.c
  system.h
  tl/algorithm.h
  tl/allocator.h
  tl/array.h
  tl/base.h
  tl/range.h
  tl/sorted_array.h
  tl/stream.h
  tl/string.h
  uuid.c
  uuid.h
  vmath.h
)
set_glob(ENGINE_INTERFACE GLOB src/engine
  config.h
  console.h
  demo.h
  engine.h
  kernel.h
  localization.h
  map.h
  masterserver.h
  message.h
  server.h
  storage.h
)
set_glob(ENGINE_SHARED GLOB src/engine/shared
  compression.cpp
  compression.h
  config.cpp
  config.h
  config_variables.h
  console.cpp
  console.h
  datafile.cpp
  datafile.h
  demo.cpp
  demo.h
  econ.cpp
  econ.h
  engine.cpp
  filecollection.cpp
  filecollection.h
  huffman.cpp
  huffman.h
  http_request.cpp
  http_request.h
  jobs.cpp
  jobs.h
  jsonparser.cpp
  jsonparser.h
  jsonwriter.cpp
  jsonwriter.h
  kernel.cpp
  linereader.cpp
  linereader.h
  map.cpp
  mapchecker.cpp
  mapchecker.h
  masterserver.cpp
  memheap.cpp
  memheap.h
  message.h
  network.cpp
  network.h
  network_client.cpp
  network_conn.cpp
  network_console.cpp
  network_console_conn.cpp
  network_server.cpp
  packer.cpp
  packer.h
  protocol.h
  ringbuffer.cpp
  ringbuffer.h
  snapshot.cpp
  snapshot.h
  storage.cpp
)
set(ENGINE_GENERATED_SHARED
  ${PROJECT_BINARY_DIR}/src/game/generated/protocol.cpp
  ${PROJECT_BINARY_DIR}/src/game/generated/protocol.h
  ${PROJECT_BINARY_DIR}/src/game/generated/protocol7.cpp
  ${PROJECT_BINARY_DIR}/src/game/generated/protocol7.h
  ${PROJECT_BINARY_DIR}/src/game/generated/protocolglue.h
)
set_glob(GAME_SHARED GLOB src/game
  collision.cpp
  collision.h
  gamecore.cpp
  gamecore.h
  layers.cpp
  layers.h
  mapitems.h
  tuning.h
  variables.h
  version.h
  voting.h
)
set(GAME_GENERATED_SHARED
  ${PROJECT_BINARY_DIR}/src/game/generated/nethash.cpp
  ${PROJECT_BINARY_DIR}/src/game/generated/protocol.h
)

set(DEPS ${ZLIB_DEP})
set(LIBS ${CMAKE_THREAD_LIBS_INIT} ${ZLIB_LIBRARIES} OpenSSL::Crypto CURL::libcurl ${PLATFORM_LIBS})

set_glob(DEP_JSON_SRC GLOB src/engine/external/json-parser
  json.c
  json.h
)
add_library(json EXCLUDE_FROM_ALL OBJECT ${DEP_JSON_SRC})
set(DEP_JSON $<TARGET_OBJECTS:json>)
list(APPEND TARGETS_OWN json)
list(APPEND DEPS ${DEP_JSON})

add_library(engine-shared EXCLUDE_FROM_ALL OBJECT
  ${ENGINE_INTERFACE} ${ENGINE_SHARED} ${ENGINE_GENERATED_SHARED} ${BASE}
)
add_library(game-shared EXCLUDE_FROM_ALL OBJECT
  ${GAME_SHARED} ${GAME_GENERATED_SHARED}
)
list(APPEND TARGETS_OWN engine-shared game-shared)

########################################################################
# SERVER
########################################################################

set_glob(ENGINE_SERVER GLOB src/engine/server
  ipcountry.cpp
  ipcountry.h
  localization.cpp
  localization.h
  register.cpp
  register.h
  server.cpp
  server.h
)
set_glob(GAME_SERVER GLOB_RECURSE src/game/server
  entities/ammo.cpp
  entities/ammo.h
  entities/battle.cpp
  entities/battle.h
  entities/bomb.cpp
  entities/bomb.h
  entities/c4.cpp
  entities/c4.h
  entities/character.cpp
  entities/character.h
  entities/check.cpp
  entities/check.h
  entities/door.cpp
  entities/door.h
  entities/flag.cpp
  entities/flag.h
  entities/guided_projectile.cpp
  entities/guided_projectile.h
  entities/health.cpp
  entities/health.h
  entities/health_shot.cpp
  entities/health_shot.h
  entities/laser.cpp
  entities/laser.h
  entities/mine.cpp
  entities/mine.h
  entities/pak.cpp
  entities/pak.h
  entities/pickup.cpp
  entities/pickup.h
  entities/projectile.cpp
  entities/projectile.h
  entities/shield.cpp
  entities/shield.h
  entities/smoke.cpp
  entities/smoke.h
  entities/supply_station.cpp
  entities/supply_station.h
  entities/switch.cpp
  entities/switch.h
  entity.cpp
  entity.h
  eventhandler.cpp
  eventhandler.h
  gamecontext.cpp
  gamecontext.h
  gamecontroller.cpp
  gamecontroller.h
  gamemodes/openbattle.cpp
  gamemodes/openbattle.h
  gameworld.cpp
  gameworld.h
  player.cpp
  player.h
)
set(GAME_GENERATED_SERVER
  ${PROJECT_BINARY_DIR}/src/game/generated/server_data.cpp
  ${PROJECT_BINARY_DIR}/src/game/generated/server_data.h
)
set(SERVER_SRC ${ENGINE_SERVER} ${GAME_SERVER} ${GAME_GENERATED_SERVER})
if(TARGET_OS STREQUAL "windows")
  set(SERVER_ICON "other/icons/teeworlds_srv_gcc.rc")
  if(MSVC)
    set(SERVER_ICON "other/icons/teeworlds_srv_cl.rc")
  endif()
else()
  set(SERVER_ICON)
endif()

set(TARGET_SERVER ${SERVER_EXECUTABLE})
add_executable(${TARGET_SERVER}
  ${DEPS}
  ${SERVER_SRC}
  ${SERVER_ICON}
  $<TARGET_OBJECTS:engine-shared>
  $<TARGET_OBJECTS:game-shared>
)
target_link_libraries(${TARGET_SERVER} ${LIBS})
list(APPEND TARGETS_OWN ${TARGET_SERVER})
list(APPEND TARGETS_LINK ${TARGET_SERVER})

if(TARGET_OS STREQUAL "mac" AND EXISTS "${PROJECT_SOURCE_DIR}/src/osxlaunch/server.m")
  find_library(COCOA Cocoa)
  set(TARGET_SERVER_LAUNCHER ${TARGET_SERVER}-Launcher)
  add_executable(${TARGET_SERVER_LAUNCHER} src/osxlaunch/server.m)
  target_link_libraries(${TARGET_SERVER_LAUNCHER} ${COCOA})
  list(APPEND TARGETS_OWN ${TARGET_SERVER_LAUNCHER})
  list(APPEND TARGETS_LINK ${TARGET_SERVER_LAUNCHER})
endif()

########################################################################
# OPTIONAL TOOLS / MASTERSERVER
########################################################################

if(EXISTS "${PROJECT_SOURCE_DIR}/src/mastersrv/mastersrv.cpp")
  set(TARGET_MASTERSRV mastersrv)
  add_executable(${TARGET_MASTERSRV} EXCLUDE_FROM_ALL
    src/mastersrv/mastersrv.cpp
    src/mastersrv/mastersrv.h
    $<TARGET_OBJECTS:engine-shared>
    ${DEPS}
  )
  target_link_libraries(${TARGET_MASTERSRV} ${LIBS})
  list(APPEND TARGETS_OWN ${TARGET_MASTERSRV})
  list(APPEND TARGETS_LINK ${TARGET_MASTERSRV})
endif()

if(TOOLS)
  set(TARGETS_TOOLS)
  set_glob(TOOLS_SRC GLOB src/tools
    crapnet.cpp
    dilate.cpp
    fake_server.cpp
    map_resave.cpp
    map_version.cpp
    packetgen.cpp
    tileset_borderfix.cpp
  )
  foreach(ABS_T ${TOOLS_SRC})
    file(RELATIVE_PATH T "${PROJECT_SOURCE_DIR}/src/tools/" ${ABS_T})
    if(T MATCHES "\\.cpp$")
      string(REGEX REPLACE "\\.cpp$" "" TOOL "${T}")
      set(TOOL_DEPS ${DEPS})
      set(TOOL_LIBS ${LIBS})
      set(TOOL_INCLUDE_DIRS)
      if(TOOL MATCHES "^(tileset_.*|dilate)$")
        list(APPEND TOOL_DEPS ${PNGLITE_DEP})
        list(APPEND TOOL_LIBS ${PNGLITE_LIBRARIES})
        list(APPEND TOOL_INCLUDE_DIRS ${PNGLITE_INCLUDE_DIRS})
      endif()
      add_executable(${TOOL} EXCLUDE_FROM_ALL
        ${TOOL_DEPS}
        src/tools/${TOOL}.cpp
        $<TARGET_OBJECTS:engine-shared>
      )
      target_include_directories(${TOOL} PRIVATE ${TOOL_INCLUDE_DIRS})
      target_link_libraries(${TOOL} ${TOOL_LIBS})
      list(APPEND TARGETS_TOOLS ${TOOL})
    endif()
  endforeach()
  list(APPEND TARGETS_OWN ${TARGETS_TOOLS})
  list(APPEND TARGETS_LINK ${TARGETS_TOOLS})
  add_custom_target(tools DEPENDS ${TARGETS_TOOLS})
endif()

add_custom_target(everything DEPENDS ${TARGETS_OWN})

########################################################################
# INSTALLATION
########################################################################

if(NOT DEV)
  install(DIRECTORY data DESTINATION share/${PROJECT_NAME} COMPONENT data)
  install(TARGETS ${TARGET_SERVER} DESTINATION bin COMPONENT server)
  install(FILES license.txt readme.txt storage.cfg DESTINATION share/${PROJECT_NAME} COMPONENT data)
endif()

########################################################################
# COMPILER-SPECIFICS
########################################################################

set(TARGETS ${TARGETS_OWN} ${TARGETS_DEP})

foreach(target ${TARGETS})
  if(MSVC)
    target_compile_options(${target} PRIVATE $<$<NOT:${DBG}>:/MT> $<${DBG}:/MTd>)
    target_compile_options(${target} PRIVATE /MP)
    target_compile_options(${target} PRIVATE /EHsc)
    target_compile_options(${target} PRIVATE /GS)
    target_compile_options(${target} PRIVATE /wd4996)
  endif()
  if(OUR_FLAGS)
    target_compile_options(${target} PRIVATE ${OUR_FLAGS})
  endif()
  if(DEFINE_FORTIFY_SOURCE)
    target_compile_definitions(${target} PRIVATE $<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=2>)
  endif()
endforeach()

foreach(target ${TARGETS_LINK})
  if(MSVC)
    set_property(TARGET ${target} APPEND PROPERTY LINK_FLAGS /SAFESEH:NO)
  endif()
  if(TARGET_OS STREQUAL "mac")
    target_link_libraries(${target} -stdlib=libc++)
    target_link_libraries(${target} -mmacosx-version-min=10.7)
  endif()
  if((MINGW OR TARGET_OS STREQUAL "linux") AND PREFER_BUNDLED_LIBS)
    target_link_libraries(${target} -static-libgcc)
    target_link_libraries(${target} -static-libstdc++)
    if(MINGW)
      target_link_libraries(${target} -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic)
    endif()
  endif()
endforeach()

foreach(target ${TARGETS_OWN})
  if(MSVC)
    target_compile_options(${target} PRIVATE /wd4244)
    target_compile_options(${target} PRIVATE /wd4267)
    target_compile_options(${target} PRIVATE /wd4800)
  endif()
  if(OUR_FLAGS_OWN)
    target_compile_options(${target} PRIVATE ${OUR_FLAGS_OWN})
  endif()
  target_include_directories(${target} PRIVATE ${PROJECT_BINARY_DIR}/src)
  target_include_directories(${target} PRIVATE src)
  target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:CONF_DEBUG>)
  target_compile_definitions(${target} PRIVATE CONF_OPENSSL)
  target_include_directories(${target} PRIVATE ${ZLIB_INCLUDE_DIRS})
  if(CURL_INCLUDE_DIRS)
    target_include_directories(${target} PRIVATE ${CURL_INCLUDE_DIRS})
  endif()
  if(OPENSSL_INCLUDE_DIR)
    target_include_directories(${target} PRIVATE ${OPENSSL_INCLUDE_DIR})
  endif()
  # Teeworlds is C++ without exceptions / RTTI expectations closer to -fno-exceptions
  if(NOT MSVC)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
  endif()
endforeach()

foreach(target ${TARGETS_DEP})
  if(MSVC)
    target_compile_options(${target} PRIVATE /W0)
  endif()
  if(OUR_FLAGS_DEP)
    target_compile_options(${target} PRIVATE ${OUR_FLAGS_DEP})
  endif()
endforeach()
