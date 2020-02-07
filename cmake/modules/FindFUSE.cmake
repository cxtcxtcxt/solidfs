if (APPLE)
  find_path(FUSE_ROOT
    NAMES include/osxfuse/fuse.h
    HINTS /usr/local
  )
  find_path(FUSE_INCLUDE_DIR
    NAMES fuse.h
    HINTS ${FUSE_ROOT}/include/osxfuse
  )
else (APPLE)
  find_path(FUSE_ROOT
    NAMES include/fuse.h
  )
  find_path(FUSE_INCLUDE_DIR
    NAMES fuse.h
    HINTS ${FUSE_ROOT}/include
  )
endif(APPLE)


if (APPLE)
  set(HINT_DIR ${FUSE_ROOT}/lib)
  find_library(FUSE_LIBRARY
    NAMES libosxfuse.dylib
    HINTS ${HINT_DIR}
  )
else (APPLE)
  set(HINT_DIR ${FUSE_ROOT}/lib)
  find_library(FUSE_LIBRARY
    NAMES libfuse3.so
    HINTS ${HINT_DIR}
  )
endif(APPLE)


include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(FUSE
  FOUND_VAR FUSE_FOUND
  REQUIRED_VARS FUSE_LIBRARY FUSE_INCLUDE_DIR
)

mark_as_advanced(FUSE_ROOT
  FUSE_LIBRARY
  FUSE_INCLUDE_DIR
)

if (FUSE_FOUND)
  message(STATUS "Found valid FUSE version:")
  message(STATUS "  FUSE root dir: ${FUSE_ROOT}")
  message(STATUS "  FUSE include dir: ${FUSE_INCLUDE_DIR}")
  message(STATUS "  FUSE libraries: ${FUSE_LIBRARY}")
endif ()
