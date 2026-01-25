// Thread collision warner - debug-only thread safety checks
// Stub implementation for standalone builds

#ifndef NET_BASE_THREADING_THREAD_COLLISION_WARNER_H_
#define NET_BASE_THREADING_THREAD_COLLISION_WARNER_H_

// These macros provide debug-only thread safety assertions.
// In production/standalone builds, they are no-ops.

#define DFAKE_MUTEX(x)
#define DFAKE_SCOPED_LOCK(x)
#define DFAKE_SCOPED_RECURSIVE_LOCK(x)

#endif  // NET_BASE_THREADING_THREAD_COLLISION_WARNER_H_
