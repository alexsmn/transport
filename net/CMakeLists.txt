find_package(Boost REQUIRED COMPONENTS system)

FILE(GLOB_RECURSE sources RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  "*.cc"
  "*.cpp"
  "*.h"
)

FILE(GLOB_RECURSE net_unittests RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  "*_unittest*"
)

FILE(GLOB_RECURSE sources_win RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  "*_win*"
  "winsock*"
)

FILE(GLOB net_sources_win_dir RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  "win/*.*"
)

LIST(APPEND sources_win
  ${net_sources_win_dir}
)

FILE(GLOB_RECURSE sources_lin RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  "*_posix*"
  "*_linux*"
)

LIST(REMOVE_ITEM sources
  ${sources_win}
  ${sources_lin}
  ${net_unittests}
)

IF(WIN32)
  LIST(APPEND sources ${sources_win})
ELSE()
  LIST(APPEND sources ${sources_lin})
ENDIF()

add_library(net ${sources})

target_compile_definitions(net PRIVATE -DNET_IMPLEMENTATION)

target_include_directories(net PUBLIC ".." ${Boost_INCLUDE_DIRS})

target_link_libraries(net base ${Boost_LIBRARIES})
