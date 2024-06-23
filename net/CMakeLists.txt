file(GLOB_RECURSE sources RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
  "*.cpp"
  "*.h"
)

file(GLOB_RECURSE SOURCES_UT RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
  "*_unittest*"
  "*_mock.*"
)

file(GLOB_RECURSE sources_win RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  "*_win*"
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
add_library(Net::net ALIAS net)

# Uses `std::span` in API.
target_compile_features(net PUBLIC cxx_std_20)

if(WIN32)
  target_compile_options(net PRIVATE /bigobj)
endif()

target_include_directories(net PUBLIC "..")

find_package(Boost REQUIRED)
target_link_libraries(net PUBLIC Boost::boost)

# UTs

add_executable(net_unittests ${SOURCES_UT})

find_package(GTest REQUIRED)

target_link_libraries(net_unittests PRIVATE
  GTest::gmock_main
  net
)

include(GoogleTest)
gtest_discover_tests(net_unittests)
