#ifndef VEC_H
#define VEC_H

#include <stdint.h>
#include <stdlib.h>

/*
 * Growing arrays. The array is a pointer, a count and a capacity that its
 * owner keeps; these helpers make room for more items, doubling the capacity.
 *
 *   Entry* items = NULL;
 *   size_t count = 0, capacity = 0;
 *   ...
 *   if (VEC_RESERVE(items, capacity, count + 1) != 0)
 *       return -1;                      // out of memory; items is still valid
 *   items[count++] = entry;
 *
 * Functions are static so that a tool takes no more than it uses.
 */

/**
 * Makes sure *capacity is at least needed items of itemSize bytes, keeping the old items.
 * Returns 0, or -1 when out of memory or the size is too big (nothing is changed).
 */
static __inline int vec_reserve(void** items, size_t* capacity, size_t needed, size_t itemSize) {
    size_t newCapacity;
    void* grown;

    if (needed <= *capacity)
        return 0;

    newCapacity = *capacity == 0 ? 16 : *capacity;
    while (newCapacity < needed) {
        if (newCapacity > SIZE_MAX / 2)
            return -1;
        newCapacity *= 2;
    }
    if (itemSize != 0 && newCapacity > SIZE_MAX / itemSize)
        return -1;

    grown = realloc(*items, newCapacity * itemSize);
    if (grown == NULL)
        return -1;
    *items = grown;
    *capacity = newCapacity;
    return 0;
}

/** VEC_RESERVE(items, capacity, needed): for an array of a typed pointer. */
#define VEC_RESERVE(items, capacity, needed) \
    vec_reserve((void**)&(items), &(capacity), (needed), sizeof *(items))

/**
 * For an array whose owner keeps no capacity, only the count (it is a power of two from 4 on):
 * makes room for one more item after count items. Returns 0, or -1 when out of memory.
 */
static __inline int vec_grow_by_count(void** items, size_t count, size_t itemSize) {
    size_t capacity;
    void* grown;

    if (count != 0 && (count < 4 || (count & (count - 1)) != 0))
        return 0;                       // there is room
    capacity = count == 0 ? 4 : count * 2;
    if (capacity > SIZE_MAX / itemSize)
        return -1;
    grown = realloc(*items, capacity * itemSize);
    if (grown == NULL)
        return -1;
    *items = grown;
    return 0;
}

/** VEC_GROW_BY_COUNT(items, count): room for one more after count items. */
#define VEC_GROW_BY_COUNT(items, count) \
    vec_grow_by_count((void**)&(items), (count), sizeof *(items))

#endif //VEC_H
