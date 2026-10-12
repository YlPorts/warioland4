extern "C" {
#include "fixed_point.h"
}
#include <cassert>
#include <iostream>

int main() {
    // Q8.8 arithmetic, compiled from original Wario Land 4 source.
    assert(FixedMul(Q_8_8(2.0), Q_8_8(1.5)) == Q_8_8(3.0));
    assert(FixedDiv(Q_8_8(3.0), Q_8_8(1.5)) == Q_8_8(2.0));
    assert(FixedInverse(Q_8_8(1.0)) == Q_8_8(1.0));
    assert(FixedMul(Q_8_8(-2.0), Q_8_8(0.5)) == Q_8_8(-1.0));
    std::cout << "Original decompiled fixed-point math OK\n";
}
