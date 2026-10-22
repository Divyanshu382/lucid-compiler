// ============================================================================
// intmath.hpp — checked 32-bit integer arithmetic, shared by the Optimizer
// (constant folding) and the Interpreter.
//
// In C++, signed overflow is undefined behaviour, and INT_MIN / -1 even traps
// the CPU and kills the program. Lucid instead detects every case: each helper
// returns false when the true result does not fit in an int, and the caller
// reports a runtime error (the optimizer simply leaves the expression
// unfolded, so the runtime reports it).
// ============================================================================
#pragma once

#include <climits>

namespace lucid {

inline bool fitsInt(long long v) { return v >= INT_MIN && v <= INT_MAX; }

inline bool checkedAdd(int a, int b, int& out) {
    long long r = static_cast<long long>(a) + b;
    if (!fitsInt(r)) return false;
    out = static_cast<int>(r);
    return true;
}
inline bool checkedSub(int a, int b, int& out) {
    long long r = static_cast<long long>(a) - b;
    if (!fitsInt(r)) return false;
    out = static_cast<int>(r);
    return true;
}
inline bool checkedMul(int a, int b, int& out) {
    long long r = static_cast<long long>(a) * b;
    if (!fitsInt(r)) return false;
    out = static_cast<int>(r);
    return true;
}
// Callers check for a zero divisor first, so they can report it separately.
inline bool checkedDiv(int a, int b, int& out) {
    if (a == INT_MIN && b == -1) return false;   // 2147483648 does not fit
    out = a / b;                                  // truncates toward zero, as in C++
    return true;
}
inline int safeMod(int a, int b) {
    return b == -1 ? 0 : a % b;                   // INT_MIN % -1 is 0, not a crash
}
inline bool checkedNeg(int a, int& out) {
    if (a == INT_MIN) return false;
    out = -a;
    return true;
}

} // namespace lucid
