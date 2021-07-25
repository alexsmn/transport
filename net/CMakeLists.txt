find_package(Boost REQUIRED)

file(GLOB_RECURSE sources RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
  "*.cc"
  "*.cpp"
  "*.h"
)

file(GLOB_RECURSE SOURCES_UT RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
  "*_unittest*"
)

file(GLOB_RECURSE sources_win RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  "*_win*"
  "winsock*"
)

list(APPEND sources_win
  "pipe_transport.cpp"
  "pipe_transport.h"
)

file(GLOB net_sources_win_dir RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
  "win/*.*"
)

list(APPEND sources_win
  ${net_sources_win_dir}
)

file(GLOB_RECURSE sources_lin RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
  "*_posix*"
  "*_linux*"
)

list(REMOVE_ITEM sources
  ${sources_win}
  ${sources_lin}
  ${SOURCES_UT}
)

if(WIN32)
  list(APPEND sources ${sources_win})
else()
  list(APPEND sources ${sources_lin})
endif()

add_library(net ${sources})

target_compile_definitions(net PRIVATE -DNET_IMPLEMENTATION)

if(BUILD_SHARED_LIBS)
  target_compile_definitions(net PUBLIC -DCOMPONENT_BUILD)
endif()

target_include_directories(net PUBLIC "..")

# TODO: Remove base
target_link_libraries(net PUBLIC base Boost::boost)

# UTs

add_executable(net_unittests ${SOURCES_UT})
target_link_libraries(net_unittests PUBLIC net gmock_main)
include(GoogleTest)
gtest_discover_tests(net_unittests)