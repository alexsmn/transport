# FindChromiumBase.cmake
# Finds or fetches the ChromiumBase library

if(TARGET ChromiumBase::base)
  return()
endif()

# Check if ChromiumBase is available externally
find_path(CHROMIUM_BASE_INCLUDE_DIR
  NAMES base/threading/thread_collision_warner.h
  PATH_SUFFIXES include
)

if(CHROMIUM_BASE_INCLUDE_DIR)
  message(STATUS "Found ChromiumBase: ${CHROMIUM_BASE_INCLUDE_DIR}")
  add_library(ChromiumBase::base INTERFACE IMPORTED)
  set_target_properties(ChromiumBase::base PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CHROMIUM_BASE_INCLUDE_DIR}"
  )
else()
  # Fetch from GitHub
  include(FetchContent)
  message(STATUS "Fetching ChromiumBase from GitHub...")

  FetchContent_Declare(
    chromebase
    GIT_REPOSITORY https://github.com/alexsmn/chromebase.git
    GIT_TAG        origin/main
    GIT_SHALLOW    TRUE
  )

  FetchContent_GetProperties(chromebase)
  if(NOT chromebase_POPULATED)
    FetchContent_Populate(chromebase)
  endif()

  add_library(ChromiumBase::base INTERFACE IMPORTED)
  set_target_properties(ChromiumBase::base PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${chromebase_SOURCE_DIR}"
  )
endif()

set(ChromiumBase_FOUND TRUE)
