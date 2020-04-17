if(NOT SDL2_FOUND)
  find_path(SDL2_INCLUDE_DIRS NAMES SDL.h
    PATHS
    ${SDL2_PKG_INCLUDE_DIRS}
    /usr/include
    /usr/local/include
    ${prefix}/include/SDL2
    /usr/include/SDL2
    ${C_INCLUDE_PATH}/SDL2
  )

# set(SDL2_LIBRARIES "-L${SDL2_LIBDIR}  -lmingw32 -lSDL2 -mwindows")

#standard library name for SDL2
set(libSDL21_library_names libSDL2 SDL2)

  find_library(SDL2_LIBRARIES
    NAMES ${libSDL21_library_names}
    PATHS
    ${SDL2_PKG_LIBRARY_DIRS}
    /usr/lib
    /usr/local/lib
    /usr/lib/x86_64-linux-gnu
    ${LIBRARY_PATH}
  )

include(CheckFunctionExists)
if(SDL2_INCLUDE_DIRS)
    set(CMAKE_REQUIRED_INCLUDES ${SDL2_INCLUDE_DIRS})
endif()
if(SDL2_LIBRARIES)
    set(CMAKE_REQUIRED_LIBRARIES ${SDL2_LIBRARIES})
endif()



if(SDL2_INCLUDE_DIRS AND SDL2_LIBRARIES)
  set(SDL2_FOUND TRUE CACHE INTERNAL "SDL2 found")
  message(STATUS "Found SDL2: ${SDL2_INCLUDE_DIRS}, ${SDL2_LIBRARIES}")
else(SDL2_INCLUDE_DIRS AND SDL2_LIBRARIES)
  set(SDL2_FOUND FALSE CACHE INTERNAL "SDL2 found")
  message(STATUS "SDL2 not found.")
endif(SDL2_INCLUDE_DIRS AND SDL2_LIBRARIES)


CHECK_FUNCTION_EXISTS("SDL_QueueAudio" SDL2_HAS_QUEUEAUDIO)
if(NOT SDL2_HAS_QUEUEAUDIO)
  set(SDL2_FOUND FALSE CACHE INTERNAL "SDL2 found")
  message(STATUS "SDL2 too old. Version 2.0.4 or up required")
  unset(SDL2_INCLUDE_DIRS)
  unset(SDL2_LIBRARIES)
endif(NOT SDL2_HAS_QUEUEAUDIO)


mark_as_advanced(SDL2_INCLUDE_DIRS SDL2_LIBRARIES)

endif(NOT SDL2_FOUND)
