#ifndef WALLOS_ATOMIC_TYPES_H
#define WALLOS_ATOMIC_TYPES_H
/*
 * Freestanding atomic interface via GCC __atomic builtins.
 * Covers all int types, including pointers.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


/*
 * Memory ordering semantics.
 *
 * These mirror GCC / C11 __ATOMIC_* values.
 *
 * In general:
 *  - Use RELAXED for pure counters / stats.
 *  - Use ACQUIRE when reading something another CPU published.
 *  - Use RELEASE when publishing data to other CPUs.
 *  - Use ACQ_REL for read-modify-write operations that both consume and publish state.
 *  - SEQ_CST really shouldn't be used.
 */
/*
 * Atomicity only. No ordering guarantees.
 *
 * Use when:
 *   - Incrementing statistics counters
 *   - Per-CPU bookkeeping
 *   - Any operation where ordering relative to other memory
 *     accesses does not matter
 *
 * Fastest option. Does NOT synchronize between CPUs.
 */
#define ATOMIC_RELAXED  __ATOMIC_RELAXED

/*
 * Data-dependency ordering only.
 *
 * Intended for pointer-chasing patterns where loaded data
 * is used to access other memory.
 *
 * Should really use ACQUIRE instead, it is treated basically the same.
 */
#define ATOMIC_CONSUME  __ATOMIC_CONSUME

/*
 * Prevents subsequent loads/stores from being reordered
 * before this operation.
 *
 * Use when:
 *   - Reading a flag or pointer published by another CPU
 *   - Lock acquisition
 *   - Consuming shared state
 *
 * Pairs with RELEASE on the publishing CPU.
 */
#define ATOMIC_ACQUIRE  __ATOMIC_ACQUIRE

/*
 * Prevents prior loads/stores from being reordered
 * after this operation.
 *
 * Use when:
 *   - Publishing initialized data to other CPUs
 *   - Making state visible to other cores
 *
 * Pairs with ACQUIRE on the consuming CPU.
 */
#define ATOMIC_RELEASE  __ATOMIC_RELEASE

/*
 * Combines ACQUIRE and RELEASE semantics.
 *
 * Use for read-modify-write operations that:
 *   - Read shared state
 *   - Modify it
 *   - Publish the result
 *
 * Typical uses:
 *   - atomic_fetch_add on refcounts
 *   - Successful CAS that transitions state
 */
#define ATOMIC_ACQ_REL  __ATOMIC_ACQ_REL

/*
 * Sequentially consistent ordering.
 *
 * Provides a single global total order across all CPUs.
 * Strongest and most expensive option.
 *
 * Really shouldn't be used. Use ACQUIRE/RELEASE instead.
 */
#define ATOMIC_SEQ_CST  __ATOMIC_SEQ_CST

static inline void atomic_fence(int order) { __atomic_thread_fence(order); }
static inline void atomic_signal_fence(int order) { __atomic_signal_fence(order); }

/***************************************************************************************************
 * Code-generation macro. Prevents us from needing to write 1000 lines of the same thing.
 *
 * Expands a complete set of operations for a given type alias and C type:
 *   atomic_load_<n>
 *   atomic_store_<n>
 *   atomic_exchange_<n>
 *   atomic_cas_<n>          (compare-and-swap, strong)
 *   atomic_cas_weak_<n>     (compare-and-swap, weak)
 *   atomic_fetch_add_<n>
 *   atomic_fetch_sub_<n>
 *   atomic_fetch_and_<n>
 *   atomic_fetch_or_<n>
 *   atomic_fetch_xor_<n>
 *   atomic_fetch_nand_<n>
 **************************************************************************************************/

#define ATOMIC_DEFINE_TYPE(name, type)                                        \
                                                                              \
static inline type                                                            \
atomic_load_##name(const type *ptr, int order)                                \
{                                                                             \
    type ret;                                                                 \
    __atomic_load(ptr, &ret, order);                                          \
    return ret;                                                               \
}                                                                             \
                                                                              \
static inline void                                                            \
atomic_store_##name(type *ptr, type val, int order)                           \
{                                                                             \
    __atomic_store(ptr, &val, order);                                         \
}                                                                             \
                                                                              \
static inline type                                                            \
atomic_exchange_##name(type *ptr, type val, int order)                        \
{                                                                             \
    type ret;                                                                 \
    __atomic_exchange(ptr, &val, &ret, order);                                \
    return ret;                                                               \
}                                                                             \
                                                                              \
static inline bool                                                            \
atomic_cas_##name(type *ptr, type *expected, type desired,                    \
                  int succ_order, int fail_order)                             \
{                                                                             \
    return __atomic_compare_exchange(ptr, expected, &desired,                 \
                                     0, succ_order, fail_order);              \
}                                                                             \
                                                                              \
static inline bool                                                            \
atomic_cas_weak_##name(type *ptr, type *expected, type desired,               \
                       int succ_order, int fail_order)                        \
{                                                                             \
    return __atomic_compare_exchange(ptr, expected, &desired,                 \
                                     1, succ_order, fail_order);              \
}                                                                             \
                                                                              \
static inline type                                                            \
atomic_fetch_add_##name(type *ptr, type val, int order)                       \
{                                                                             \
    return __atomic_fetch_add(ptr, val, order);                               \
}                                                                             \
                                                                              \
static inline type                                                            \
atomic_fetch_sub_##name(type *ptr, type val, int order)                       \
{                                                                             \
    return __atomic_fetch_sub(ptr, val, order);                               \
}                                                                             \
                                                                              \
static inline type                                                            \
atomic_fetch_and_##name(type *ptr, type val, int order)                       \
{                                                                             \
    return __atomic_fetch_and(ptr, val, order);                               \
}                                                                             \
                                                                              \
static inline type                                                            \
atomic_fetch_or_##name(type *ptr, type val, int order)                        \
{                                                                             \
    return __atomic_fetch_or(ptr, val, order);                                \
}                                                                             \
                                                                              \
static inline type                                                            \
atomic_fetch_xor_##name(type *ptr, type val, int order)                       \
{                                                                             \
    return __atomic_fetch_xor(ptr, val, order);                               \
}                                                                             \
                                                                              \
static inline type                                                            \
atomic_fetch_nand_##name(type *ptr, type val, int order)                      \
{                                                                             \
    return __atomic_fetch_nand(ptr, val, order);                              \
}

/* -------------------------------------------------------------------------
 * Instantiate for every standard integer width, signed and unsigned.
 * ------------------------------------------------------------------------- */

ATOMIC_DEFINE_TYPE(u8, uint8_t)
ATOMIC_DEFINE_TYPE(u16, uint16_t)
ATOMIC_DEFINE_TYPE(u32, uint32_t)
ATOMIC_DEFINE_TYPE(u64, uint64_t)

ATOMIC_DEFINE_TYPE(i8, int8_t)
ATOMIC_DEFINE_TYPE(i16, int16_t)
ATOMIC_DEFINE_TYPE(i32, int32_t)
ATOMIC_DEFINE_TYPE(i64, int64_t)

// uintptr_t / intptr_t cover the native word width on every GCC target.
// These also serve as the correct substrate for pointer arithmetic.
// Callers should cast their typed pointer through uintptr_t rather than
// relying on a void * wrapper that would require aliasing casts internally.

ATOMIC_DEFINE_TYPE(uptr, uintptr_t)
ATOMIC_DEFINE_TYPE(iptr, intptr_t)

#undef ATOMIC_DEFINE_TYPE

// This is here to make my linter not get confused by the macros.
// It tries to indent everything below this otherwise.
;

static inline void* atomic_load_ptr(void* const* ptr, int order) {
    void* ret;
    __atomic_load(ptr, &ret, order);
    return ret;
}

static inline void atomic_store_ptr(void** ptr, void* val, int order) {
    __atomic_store(ptr, &val, order);
}

static inline void* atomic_exchange_ptr(void** ptr, void* val, int order) {
    void* ret;
    __atomic_exchange(ptr, &val, &ret, order);
    return ret;
}

/*
 * *expected is read on entry and written with the current value on failure,
 * identical semantics to C11 atomic_compare_exchange_strong.
 */
static inline bool atomic_cas_ptr(void** ptr, void** expected, void* desired, int succ_order, int fail_order) {
    return __atomic_compare_exchange(ptr, expected, &desired, 0, succ_order, fail_order);
}

static inline bool atomic_cas_weak_ptr(void** ptr, void** expected, void* desired, int succ_order, int fail_order) {
    return __atomic_compare_exchange(ptr, expected, &desired, 1, succ_order, fail_order);
}


#endif // WALLOS_ATOMIC_TYPES_H