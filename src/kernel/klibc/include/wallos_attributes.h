#ifndef WALLOS_ATTRIBUTES_H
#define WALLOS_ATTRIBUTES_H

/* This file provides common attributes (mostly functions) we need for C. A lot of these mimic the C++/C23 attributes.
 * Comments are for clangd (for IDE hovering), most of these are obvious.
 * I plan on this header probably being used in the userside/module side as well, so some of these really aren't useful now.
 * Some are also just included because I was looking through the GCC attribute list and thought they were weird/interesting.
 */

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Function Attributes
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/* All interrupt handlers must be marked with both these attributes, and it's incredibly ugly to have them decorated normally. */
#define WALLOS_INTERRUPT_HANDLER __attribute__((interrupt)) __attribute__((__target__("general-regs-only")))

/* Control flow */
#define WALLOS_NORETURN __attribute__((noreturn)) //< Function doesnt return
#define WALLOS_HOT      __attribute__((hot))      //< This function is in a hot path
#define WALLOS_NAKED    __attribute__((naked))    //< Body is pure inline asm. probably never used.

/* Inlining */
#define WALLOS_NOINLINE      __attribute__((noinline))                    //< Never inline this function
#define WALLOS_ALWAYS_INLINE static inline __attribute__((always_inline)) //< Always inline
#define WALLOS_FLATTEN       __attribute__((flatten))                     //< Inline every call made inside this function

/* Linking */
#define WALLOS_USED          __attribute__((used))           //< Keep even if unreferenced (like only called from asm)
#define WALLOS_UNUSED        __attribute__((unused))         //< Silence unused-function warnings
#define WALLOS_WEAK          __attribute__((weak))           //< Overridable by a strong definition elsewhere
#define WALLOS_ALIAS(target) __attribute__((alias(#target))) //< Make this symbol another name for target. Look up an example for this if you need to use it.
#define WALLOS_SECTION(name) __attribute__((section(name)))  //< Place in a named linker section
#define WALLOS_CONSTRUCTOR   __attribute__((constructor))    //< Run automatically via the init array at startup. SHOULD NOT be used in kernel. Only here for the ABI.

/* Checks and warnings */
#define WALLOS_NODISCARD                 __attribute__((warn_unused_result))         //< Warn if the return value is ignored
#define WALLOS_DEPRECATED(msg)           __attribute__((deprecated(msg)))            //< Warn on use, showing msg
#define WALLOS_RETURNS_NONNULL           __attribute__((returns_nonnull))            //< Return value is never NULL
#define WALLOS_FORMAT_STRING(fmt, first) __attribute__((format(printf, fmt, first))) //< printf-style format checking (1-based arg indices). This will drop compiler warnings for incorrect params being passed to the function based on the format string.
// intentionally left out __attribute__((nonnull(__VA_ARGS__))). I dont like the potential for misuse and UB.

/* Optimizer hints */
// The compiler should probably figure this out for us. These really shouldn't be used unless we're certain that the compiler needs the hints.
#define WALLOS_READS_MEMORY_ONLY __attribute__((pure))  //< No side effects. May read memory (args and globals).
#define WALLOS_NO_MEMORY_ACCESS  __attribute__((const)) //< No side effects and no memory reads. Result depends only on arg values.

/* Allocators */
// These shouldn't really be used outside the kernel/user allocators
#define WALLOS_MALLOC          __attribute__((malloc))                  //< Returned pointer doesn't alias anything else
#define WALLOS_ALLOC_SIZE(...) __attribute__((alloc_size(__VA_ARGS__))) //< Which arg(s) give the allocation size

/* Instrumentation opt-outs */
// These also really shouldn't be used but still wanted them in the ABI.
// Look them up if they need to be used.
#define WALLOS_NO_STACK_PROTECTOR __attribute__((no_stack_protector))
#define WALLOS_NO_UBSAN           __attribute__((no_sanitize("undefined")))
#define WALLOS_NO_INSTRUMENT      __attribute__((no_instrument_function))

/* x86 ABI specific */
#define WALLOS_MS_ABI   __attribute__((ms_abi))   //< Microsoft x64 calling convention
#define WALLOS_SYSV_ABI __attribute__((sysv_abi)) //< System V calling convention (default, shouldn't really be used unless mixing them)

/* Preserves every register, mostly for helpers called from asm/interrupt paths */
#define WALLOS_NO_CALLER_SAVED __attribute__((no_caller_saved_registers, __target__("general-regs-only")))

// Not really a function attribute but still wanted it here.
#define WALLOS_FALLTHROUGH __attribute__((fallthrough)) //< Intentional switch fallthrough

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Misc Attributes
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

#define WALLOS_ALIGNED(n)        __attribute__((aligned(n)))        //< Align variable/type to n bytes
#define WALLOS_ASSUME_ALIGNED(n) __attribute__((assume_aligned(n))) //< Function returns a pointer aligned to n
#define WALLOS_PACKED            __attribute__((packed))            //< Pack the array

#define WALLOS_CLEANUP(fn) __attribute__((cleanup(fn))) //< Scope-based cleanup. Seems useful but not necessarily super readable. Still want in this for modules.

#define WALLOS_MAY_ALIAS       __attribute__((may_alias))        //< Type may alias any other type (exempt from strict aliasing)
#define WALLOS_DESIGNATED_INIT __attribute__((designated_init))  //< Struct must be initialized with .field = value syntax
#define WALLOS_NONSTRING       __attribute__((nonstring))        //< char array is not NUL-terminated (silences strncpy-style warnings)

#define WALLOS_BIG_ENDIAN_STRUCT    __attribute__((scalar_storage_order("big-endian")))    //< Struct has big endian byte ordering
#define WALLOS_LITTLE_ENDIAN_STRUCT __attribute__((scalar_storage_order("little-endian"))) //< Struct has little endian byte ordering

#define WALLOS_ERROR(msg)   __attribute__((error(msg)))    //< Compile error if a call survives optimization
#define WALLOS_WARNING(msg) __attribute__((warning(msg)))  //< Compile warning if a call survives optimization

#define WALLOS_RETURNS_TWICE __attribute__((returns_twice)) //< This is mostly setjmp weirdness. I wanted this because I thought it was weird. If I see this in code, I will be very annoyed.
#define WALLOS_NOIPA         __attribute__((noipa))          //< No inlining, cloning, or cross-function assumptions.

/* Useful for security. Helps prevent ROP. */
#define WALLOS_ZERO_REGS_ON_RETURN __attribute__((zero_call_used_regs("used-gpr"))) //< Zero used registers before returning from this function

#endif // WALLOS_ATTRIBUTES_H