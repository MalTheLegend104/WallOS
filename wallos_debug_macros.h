/* This file is forcefully included along with every single file.
 *
 * This is simply so that I can use these debug macros anywhere, any time, without having to with including another header and remembering to remove it when I'm done.
 */
#pragma once

#define WALLOS_SPIN_PAUSE()	__asm__ volatile("pause")

#define WALLOS_HLT()        __asm__ volatile("hlt")
#define WALLOS_CLI()        __asm__ volatile("cli")
#define WALLOS_STI()        __asm__ volatile("sti")
#define WALLOS_CLI_HLT()    do { WALLOS_CLI(); WALLOS_HLT(); } while (0)

#define WALLOS_HANG() do { __asm__ volatile("cli"); for (;;) __asm__ volatile("hlt"); } while (0)

/* I find myself constantly needing to extern this for debugging in libraries. */

#define WALLOS_RET_ADDR() __builtin_return_address(0)

#define WALLOS_STATIC_ASSERT(cond, msg) typedef char static_assert_##msg[(cond) ? 1 : -1]
