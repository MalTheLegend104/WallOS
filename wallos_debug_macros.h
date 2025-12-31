/* This file is forcefully included along with every single file.
 *
 * This is simply so that I can use these debug macros anywhere, any time, without having to with including another header and remembering to remove it when I'm done.
 */
#pragma once

#define WALLOS_HLT()        asm volatile("hlt")
#define WALLOS_CLI()        asm volatile("cli")
#define WALLOS_STI()        asm volatile("sti")
#define WALLOS_CLI_HLT()    do { WALLOS_CLI(); WALLOS_HLT(); } while (0)

#define WALLOS_HANG() do { asm volatile("cli"); for (;;) asm volatile("hlt"); } while (0)

#define WALLOS_STATIC_ASSERT(cond, msg) typedef char static_assert_##msg[(cond) ? 1 : -1]