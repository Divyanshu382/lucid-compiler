// ============================================================================
// bigstack.hpp — run a function on a thread with a large stack.
//
// Every phase walks the syntax tree recursively, and a Lucid function call
// recurses in the interpreter. Default stacks are small and differ by platform
// (macOS worker threads 512 KB, the Windows main thread 1 MB), so a
// recursive Lucid program could overflow the C++ stack and kill the server.
// The CLI and the server therefore run each compilation through this helper.
// ============================================================================
#pragma once

#include <functional>

namespace lucid {

// Runs `fn` to completion on a new thread with a 256 MB stack reservation
// (pages are only committed as they are used). An exception thrown by `fn`
// is re-thrown in the caller.
void runOnLargeStack(const std::function<void()>& fn);

} // namespace lucid
