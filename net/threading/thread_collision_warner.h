// Thread collision warner - debug-only thread safety checks
// Stub implementation for standalone builds

#ifndef NET_BASE_THREADING_THREAD_COLLISION_WARNER_H_
#define NET_BASE_THREADING_THREAD_COLLISION_WARNER_H_

// These macros provide debug-only thread safety assertions.
// In production/standalone builds, they are no-ops.

// Define a dummy type so DFAKE_MUTEX creates an actual member
// that can be referenced (e.g., in #if !defined(NDEBUG) blocks)
namespace base {
struct DfakeMutex {};
}  // namespace base

#define DFAKE_MUTEX(x) ::base::DfakeMutex x
#define DFAKE_SCOPED_LOCK(x) (void)(x)
#define DFAKE_SCOPED_RECURSIVE_LOCK(x) (void)(x)

#endif  // NET_BASE_THREADING_THREAD_COLLISION_WARNER_H_
