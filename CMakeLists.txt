cmake_minimum_required(VERSION 3.25)

cmake_policy(SET CMP0167 NEW)

# The shared build-support kit (ADR 0011): at this product's root in an export,
# at the tree root in the monorepo. Before it, net had no C++ standard and no
# cppcheck configuration of its own, so built inside `core` it inherited core's
# — including a suppressions file it does not have and cannot see.
set(_scada_dir "${CMAKE_CURRENT_SOURCE_DIR}")
while(NOT EXISTS "${_scada_dir}/build-support/ScadaProducts.cmake")
  get_filename_component(_scada_parent "${_scada_dir}" DIRECTORY)
  if(_scada_parent STREQUAL _scada_dir)
    message(FATAL_ERROR
      "build-support/ not found above ${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
  set(_scada_dir "${_scada_parent}")
endwhile()
include("${_scada_dir}/build-support/ScadaProducts.cmake")
include("${_scada_dir}/build-support/ScadaProductBase.cmake")
unset(_scada_dir)
unset(_scada_parent)

scada_product_prologue()

project(transport VERSION 1.0.0 LANGUAGES CXX)

scada_product_base()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

enable_testing()

add_subdirectory(transport)
