#pragma once

#if defined(__clang__)
#define TRANSPORT_COMPILER_CLANG
#elif defined(__GNUC__) || defined(__GNUG__)
#define TRANSPORT_COMPILER_GCC
#elif defined(_MSC_VER)
#define TRANSPORT_COMPILER_MSVC
#endif

#if defined(TRANSPORT_COMPILER_GCC) || defined(TRANSPORT_COMPILER_CLANG)
#define PRINTF_FORMAT(format_param, dots_param) \
  __attribute__((format(printf, format_param, dots_param)))
#else
#define PRINTF_FORMAT(format_param, dots_param)
#endif

#if !defined(TRANSPORT_COMPILER_MSVC)
#define _Printf_format_string_
#endif
