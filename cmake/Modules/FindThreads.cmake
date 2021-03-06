IF(WIN32 AND NOT CYGWIN AND THREADS_USE_PTHREADS_WIN32)
  SET(_Threads_ptwin32 true)
ENDIF()

if(NOT Threads_FOUND)
  find_path(THREADS_PTHREADS_INCLUDE_DIR NAMES pthread.h
    PATHS
    /usr/include
    /usr/local/include
    ${C_INCLUDE_PATH}
  )

#standard library name for pthrreads

IF(_Threads_ptwin32)
set(pthreads_library_names libpthreadGC-3.a libwinpthread.a)
ELSE()
set(pthreads_library_names pthread)
ENDIF()


  find_library(CMAKE_THREAD_LIBS_INIT
    NAMES ${pthreads_library_names}
    PATHS
    /usr/lib
    /usr/local/lib
    ${LIBRARY_PATH}
  )

include(CheckFunctionExists)
if(THREADS_PTHREADS_INCLUDE_DIRS)
    set(CMAKE_REQUIRED_INCLUDES ${THREADS_PTHREADS_INCLUDE_DIRS})
endif()
if(CMAKE_THREAD_LIBS_INIT)
    set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})
endif()



if(THREADS_PTHREADS_INCLUDE_DIR AND CMAKE_THREAD_LIBS_INIT)
  set(Threads_FOUND TRUE CACHE INTERNAL "pthreads found")
  message(STATUS "Found pthreads: ${THREADS_PTHREADS_INCLUDE_DIR}, ${CMAKE_THREAD_LIBS_INIT}")
else(THREADS_PTHREADS_INCLUDE_DIR AND CMAKE_THREAD_LIBS_INIT)
  set(Threads_FOUND FALSE CACHE INTERNAL "pthreads found")
  message(STATUS "pthreads not found.")
endif(THREADS_PTHREADS_INCLUDE_DIR AND CMAKE_THREAD_LIBS_INIT)

mark_as_advanced(THREADS_PTHREADS_INCLUDE_DIR CMAKE_THREAD_LIBS_INIT)

endif(NOT Threads_FOUND)
