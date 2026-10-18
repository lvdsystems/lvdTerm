# - Try to find libcbor
# Once done this will define
#  LIBCBOR_FOUND - libcbor was found
#  LIBCBOR_INCLUDE_DIRS - libcbor include directories
#  LIBCBOR_LIBRARIES - libraries needed for linking

find_package(PkgConfig)

if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_LIBCBOR QUIET libcbor)
endif()

find_path(LIBCBOR_INCLUDE_DIR cbor.h HINTS ${PC_LIBCBOR_INCLUDEDIR} ${PC_LIBCBOR_INCLUDE_DIRS})
set(LIBCBOR_INCLUDE_DIRS ${LIBCBOR_INCLUDE_DIR})

# Check version (optional)
if(LIBCBOR_INCLUDE_DIR)
  file(READ "${LIBCBOR_INCLUDE_DIR}/cbor/configuration.h" VERSION_CONTENT)
  string(REGEX MATCH "CBOR_MAJOR_VERSION([ \t]*)([0-9]+)" _ "${VERSION_CONTENT}")
  set(LIBCBOR_VERSION_MAJOR ${CMAKE_MATCH_2})
  string(REGEX MATCH "CBOR_MINOR_VERSION([ \t]*)([0-9]+)" _ "${VERSION_CONTENT}")
  set(LIBCBOR_VERSION_MINOR ${CMAKE_MATCH_2})
  string(REGEX MATCH "CBOR_PATCH_VERSION([ \t]*)([0-9]+)" _ "${VERSION_CONTENT}")
  set(LIBCBOR_VERSION_PATCH ${CMAKE_MATCH_2})

  set(LIBCBOR_VERSION "${LIBCBOR_VERSION_MAJOR}.${LIBCBOR_VERSION_MINOR}.${LIBCBOR_VERSION_PATCH}")
endif()

find_library(LIBCBOR_LIBRARY NAMES cbor HINTS ${PC_LIBCBOR_LIBDIR} ${PC_LIBCBOR_LIBRARY_DIRS})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  libcbor REQUIRED_VARS LIBCBOR_LIBRARY LIBCBOR_INCLUDE_DIR LIBCBOR_INCLUDE_DIRS VERSION_VAR LIBCBOR_VERSION
)

set(LIBCBOR_LIBRARIES ${LIBCBOR_LIBRARY})
set(LIBCBOR_INCLUDE_DIRS ${LIBCBOR_INCLUDE_DIR})

mark_as_advanced(LIBCBOR_INCLUDE_DIR LIBCBOR_LIBRARY)
