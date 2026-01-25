// Compiler-specific macros
// Standalone implementation

#ifndef NET_BASE_COMPILER_SPECIFIC_H_
#define NET_BASE_COMPILER_SPECIFIC_H_

// Printf format attribute for compile-time format string checking
#if defined(__GNUC__) || defined(__clang__)
#define PRINTF_FORMAT(format_param, dots_param) \
    __attribute__((format(printf, format_param, dots_param)))
#else
#define PRINTF_FORMAT(format_param, dots_param)
#endif

// MSVC SAL annotation for printf format strings
#ifndef _Printf_format_string_
#define _Printf_format_string_
#endif

#endif  // NET_BASE_COMPILER_SPECIFIC_H_
