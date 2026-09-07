#pragma once
#include <cstdint>

namespace bighero {

// Compose a value into a 64-bit opaque render sort key: the high bits
// carry a draw-order / layer priority, the low bits carry a secondary key
// (e.g. depth or material). Used to make stable front-to-back or
// back-to-front ordering cheap to compare.
class SortKey {
public:
    // Build from a draw order (high bits) and a raw secondary key (low bits).
    static uint64_t Make(int drawOrder, uint32_t secondary = 0) {
        uint64_t order = (uint64_t)(uint32_t)drawOrder & 0x7FFFFFFFu;
        return (order << 32) | (secondary & 0xFFFFFFFFull);
    }

    // Extract the primary (draw order) component.
    static int Order(uint64_t key) { return (int)(key >> 32); }

    // Extract the secondary component.
    static uint32_t Secondary(uint64_t key) { return (uint32_t)(key & 0xFFFFFFFFull); }

    // Compare two keys; returns -1/0/1 based on primary order, then secondary.
    static int Compare(uint64_t a, uint64_t b) {
        if (a < b) return -1;
        if (a > b) return 1;
        return 0;
    }

    // Reorder key for transparency blend (swap ascending/descending intent).
    static uint64_t ForTransparency(int drawOrder, uint32_t depth) {
        // Higher depth renders first for transparency (back-to-front).
        uint32_t inv = 0xFFFFFFFFu - depth;
        return Make(drawOrder, inv);
    }
};

} // namespace bighero
