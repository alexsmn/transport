if(NOT BOOST_FOUND)
  find_package(Boost REQUIRED COMPONENTS system)
endif()

file(GLOB_RECURSE sources RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  "*.cc"
  "*.cpp"
  "*.h"
)

file(GLOB_RECURSE net_unittests RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  "*_unittest*"
)

file(GLOB_RECURSE sources_win RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  "*_win*"
  "winsock*"
)

list(APPEND sources_win
  "pipe_transport.cpp"
  "pipe_transport.h"
  "serial_transport.cpp"
  "serial_transport.h"
  "socket.cpp"
  "socket.h"
  "socket_delegate.h"
  "socket_handle.h"
  "socket_pool.cpp"
  "socket_pool.h"
  "socket_transport.cpp"
  "socket_transport.h"
  "socket_window.h"
)

file(GLOB net_sources_win_dir RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  "win/*.*"
)

list(APPEND sources_win
  ${net_sources_win_dir}
)

file(GLOB_RECURSE sources_lin RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  "*_posix*"
  "*_linux*"
)

list(REMOVE_ITEM sources
  ${sources_win}
  ${sources_lin}
  ${net_unittests}
)

if(WIN32)
  list(APPEND sources ${sources_win})
else()
  list(APPEND sources ${sources_lin})
endif()

add_library(net ${sources})

if(WIN32)
  target_compile_options(net PUBLIC "/std:c++latest") 
endif()

target_compile_definitions(net PRIVATE -DNET_IMPLEMENTATION)

if(BUILD_SHARED_LIBS)
  target_compile_definitions(net PUBLIC -DCOMPONENT_BUILD)
endif()

target_include_directories(net PUBLIC "..")

target_link_libraries(net PUBLIC base Boost::boost Boost::system)
