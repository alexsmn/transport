# FindPromiseHpp.cmake
# Finds or fetches the promise.hpp library

if(TARGET promise.hpp)
  return()
endif()

# Check if promise.hpp is available externally
find_path(PROMISE_HPP_INCLUDE_DIR
  NAMES promise.hpp/promise.hpp
  PATH_SUFFIXES include headers
)

if(PROMISE_HPP_INCLUDE_DIR)
  message(STATUS "Found promise.hpp: ${PROMISE_HPP_INCLUDE_DIR}")
  add_library(promise.hpp INTERFACE IMPORTED)
  set_target_properties(promise.hpp PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${PROMISE_HPP_INCLUDE_DIR}"
  )
else()
  # Fetch from GitHub
  include(FetchContent)
  message(STATUS "Fetching promise.hpp from GitHub...")

  FetchContent_Declare(
    promise_hpp
    GIT_REPOSITORY https://github.com/alexsmn/promise.hpp.git
    GIT_TAG        origin/main
    GIT_SHALLOW    TRUE
  )

  FetchContent_GetProperties(promise_hpp)
  if(NOT promise_hpp_POPULATED)
    FetchContent_Populate(promise_hpp)
  endif()

  add_library(promise.hpp INTERFACE)
  target_include_directories(promise.hpp INTERFACE "${promise_hpp_SOURCE_DIR}/headers")
endif()

set(PromiseHpp_FOUND TRUE)
