# Code Style

## Table of Contents

- [Assembly Code](#assembly)
- [Indentation](#indentation)
- [Names](#names)
- [General Rules (braces, indentation levels)](#braces--general-rules)
- [Alignment and Grouping](#alignment-and-grouping)

## Assembly
- All assembly code *that is in its own source file*, should use [NASM](https://github.com/netwide-assembler/nasm) syntax. 
  - NASM syntax is essentially the `intel` syntax, with a few additional keywords.
  - If something *must* be compiled with [`gas`](https://en.wikipedia.org/wiki/GNU_Assembler), use the AT&T syntax.
    - NASM should be able to handle virtually everything we need asm for. The only exception is the `CRTI` & `CRTEND` needed by GCC.
- All *inline* assembly must be in the AT&T syntax, as that's what `gas` expects.
  - Technically you can force inline asm to be intel syntax, but changing that now would cause a major refactoring.

## Indentation

WallOS uses tabs.

## Names

### `namespace`

- Names use PascalCase
- Functions use camelCase

### `class`

- Names use PascalCase
- Methods use camelCase

### `function`

#### C:

- snake_case or camelCase
  - Anything that may be referenced by C++ code must be camelCase
  - Function "overloads", must use snake_case:
    ```C
    // panic "string"
    void panic_s(const char* buf);
    // Panic "string array"
    void panic_sa(const char** buf, uint8_t length);
    // Panic "int"
    void panic_i(uint8_t panic);
    ```

    - The name of the function before the "overload" can be camelCase:
      ```C
      void exampleCommand_i(int a);
      void exampleCommand_c(char a);
      ```
- Other than those exceptions, the codebase is a weird mix of both. Use either as you prefer.
  - Try to keep it consistent throughout a file, namespace, or class.

#### C++

- camelCase
  - If defined as `extern "C"`, and is to be called by C code, it can be snake_case.

### `enum` & `struct`

- Depending on context:
  - PascalCase if name is one word, makes sense as "object"
    ```C
    typedef struct {
        int (*mainCommand)(int argc, char** argv);
        int (*helpCommand)(int argc, char** argv);
        const char* commandName;
        const char** aliases;
        size_t aliases_count;
    } Command;
    ```

    - This is "like an object" because it gets passed as a function, contains function pointers, and static variables.

    > If the struct is a C++ struct, it counts as an object.
    >
  - snake_case
    - Every other scenario.
- Internals of `enum` should be in SCREAMING_SNAKE_CASE
- Internals of `struct` should be in snake_case
  - The exception is functions contained in structs. The example struct above contains two function pointers.
    These function pointers should be named with the same conventions as [functions](#function).

### Union

- All unions should be in snake_case

### Local Variables and Arguments

All local variables and arguments should be either camelCase or snake_case. Use either as you prefer.

### Global Variables and `#define`

All global variables and `#define` should be in SCREAMING_SNAKE_CASE

- This includes any macros, excluding the standard library (like assert).

## Braces & General Rules

The code style for this project is largely based on [K&amp;R](https://en.wikipedia.org/wiki/Indentation_style#K&R_style), particularly the "one true brace style" variant.

> Anything enclosed by braces is expected to be indented one level past the previous indentation level

### Functions

- Starting brace on the same line as the definition
- A space should follow the closing parenthesis
- The end brace should be either a part of the same line, or indented at the same level as the start of the definition.
- Functions that take no parameters can either include `void` in the parentheses or leave them empty.
- One-liners are allowed for functions that have a single statement.

```C
void exampleFunction(void) {
    // code here
}
// OR
int exampleFunction2() { return 5; }
```

- It is acceptable to split parameters across lines, only if there is several of them (5+)
  - Braces must follow the same rules as before
  - The parameters should be 1 indent from the start of the function definition
    ```C
    void someFunction(
        int param1,
        char param2,
        char* param3,
        ClassName* param4,
        bool param5) {
        // code here
    }
    ```

### Structs & Enums

- Generally structs/enums should be `typedef`, unless there is a good reason not to.
- Same as a function definition, the starting brace is on the same line as the definition, with a space before it.
- The name and any attributes should come after the closing brace, which is aligned with the definition.
  - In some cases, it's required to put the name before the opening brace:
    ```C
    typedef struct Block {
        uintptr_t pointer;
        bool free;
        Block* next_block;
    } __attribute__((packed)) Block;
    ```

    > This linked list needs to know what a Block is before it would be defined.
    >

```C
typedef struct {
    // Struct internals
} __attribute__((packed)) example_struct;

typedef enum {
    // enum internals
} example_enum;
```

### Classes

- Starting brace on the same line as the definition, with a space before it.
- Private members should come before public ones.
  - Occasionally it comes up that you need to define something publicly that then referenced in a private declaration.
  - In this case, it is of course perfectly acceptable to define that before the private members.
    Make sure to put only the necessary members before the private ones, and everything where it would normally go.
- Private and public labels, along with the closing brace, should be on the same indentation level as the definition.
- Constructors and Deconstructors should be treated like functions.
```C++
class ExampleClass{
private:
    int private_variable;
    void privateMethod();
public:
    class SubClass{
    private:
        // private subclass internals
    public:
        // public subclass internals
    };
    int public_variable;
    void publicMethod();
};
```

### Namespaces

- Starting brace on the same line as the definition, with a space before it.
- Closing brace on the same level as the definition.

```C++
namespace Example {
    // namespace internals
}
```

### If/Else

- Starting brace on the same line as the definition.
- A space should follow the closing parenthesis.
- `else` and `else if` should be on the same line as the closing brace for the previous statement.
  - There should be a space between the closing brace and next statement.

```C
if (expression) {

} else if (another_expression) {

} else {

}
```

- Single line if/else without braces are allowed, only if each contains one statement only.
  - If the `if` part only contains one statement, but the `else` contains more, both should have braces.
    ```C
    if (expression) return;
    // or
    if (expression) some_variable = 5;
    else if (another_expression) break;
    // or
    if (expression) {
        break;
    } else {
        some_variable = 5;
        return;
    }
    ```

    > In the last case above, the `else` contains two statements, therefore the `if` part must also have braces.
    > This goes both ways, if the `else` only has one statement but the `if` has more, both must have braces.
    >

### Switch Case
- Starting brace on the same line as the definition.
- A space should follow the closing parenthesis.
- Case labels should be indented a level in from the switch statement.
- Case statements can be surrounded in braces if there is more than two statements inside the case.
  - This is required for very long cases (10+ statements).
  - These braces follow the same rules as everything else: same line as definition with a space before it, and ends on same level as definition.

```C
switch (expression) {
    case CASE_1:
    case CASE_2: {
      // imagine 10+ lines here
      break;
    }
    case CASE_3:
        some_var = 5;
        break;
    default:
        break;
}
```

### Labels

- `Labels` and `goto` should be used in a last case scenario, or for a ***very*** good reason.
- `Labels` should be one indentation level *behind* the surrounding code.
```C
void example() {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 3; k++) {
                for (int l = 0; l < 3; l++) {
                    if (condition) {
                        // Some code that needs to exit the loops
                        goto exitLoops;
                    }
                }
            }
        }
    }

exitLoops:
    // rest of code
}
```

## Alignment and Grouping

In chunks of code that are closely related, it's often easier to read if they are aligned & grouped together.
- Take this chunk of code from `virtual_mem.hpp`:
  ```C
  #define KERNEL_VIRTUAL_BASE 0xFFFFFFFF80000000ULL
  // The first 52 bytes of memory: 0b1111111111111111111111111111111111111111000000000000
  #define PAGE_FRAME 0xFFFFFFFFFF000ULL
  #define TABLE_ENTRIES 512 

  /* Macros to make page modification not magic. */
  #define GET_PML4_INDEX(page)         (((page) >> 39) & 0x1FF)
  #define GET_PDPT_INDEX(page)         (((page) >> 30) & 0x1FF)
  #define GET_PAGE_DIR_INDEX(page)     (((page) >> 21) & 0x1FF)
  #define GET_PAGE_TABLE_INDEX(page)   (((page) >> 12) & 0x1FF)

  #define BIT_NX                     0x8000000000000000ULL // Highest bit, bit 63
  #define BIT_11                     0x800ULL
  #define BIT_10                     0x400ULL
  #define BIT_9                      0x200ULL
  #define BIT_GLOBAL                 0x100ULL
  #define BIT_SIZE                   0x80ULL
  #define BIT_DIRTY                  0x40ULL
  #define BIT_ACCESS                 0x20ULL
  #define BIT_PCD                    0x10ULL
  #define BIT_PWT                    0x08ULL
  #define BIT_USR                    0x04ULL
  #define BIT_WRITE                  0x02ULL
  #define BIT_PRESENT                0x01ULL

  #define SET_BIT_NX(page)           (page = (page | BIT_NX))
  #define SET_BIT_11(page)           (page = (page | BIT_11))
  #define SET_BIT_10(page)           (page = (page | BIT_10))
  #define SET_BIT_9(page)            (page = (page | BIT_9))
  #define SET_BIT_GLOBAL(page)       (page = (page | BIT_GLOBAL))
  #define SET_BIT_SIZE(page)         (page = (page | BIT_SIZE))
  #define CLEAR_BIT_DIRTY(page)      (page = (page & ~BIT_DIRTY))
  #define CLEAR_BIT_ACCESS(page)     (page = (page & ~BIT_ACCESS))
  #define SET_BIT_PCD(page)          (page = (page | BIT_PCD))
  #define SET_BIT_PWT(page)          (page = (page | BIT_PWT))
  #define SET_BIT_USR(page)          (page = (page | BIT_USR))
  #define SET_BIT_WRITE(page)        (page = (page | BIT_WRITE))
  #define SET_BIT_PRESENT(page)      (page = (page | BIT_PRESENT))

  #define PAGE_4KB_SIZE 0x1000 
  #define PAGE_2MB_SIZE 0x200000   // 512 * 4096
  #define PAGE_1GB_SIZE 0x40000000 // 512 * 512 * 4096
  ```
  > This example shows both alignment and grouping.
  > 
  > The group of defines for `BIT_*` are all grouped together, with space above and below them.
  > The same can be said for the `SET_BIT_*` macros. 
  > 
  > Both of these groups have their macros aligned too, so you can go down in a straight line and compare them
  - This doesn't apply to just `#define`, this can apply to groups of functions, variables, classes, etc.

- Things don't always look good or are easy to read if directly next to each other, regardless of if they are related or not.
  - Take these structs for example:
	```C
	typedef struct {
	    int (*mainCommand)(int argc, char** argv);
	    int (*helpCommand)(int argc, char** argv);
	    const char* commandName;
	    const char** aliases;
	    size_t aliases_count;
	} Command;
	
	typedef struct {
	    const char* commandName;
	    const char* description;
	    const char** commands;
	    const int commands_count;
	    const char** aliases;
	    const int aliases_count;
	} HelpEntryGeneral;
	
	typedef struct {
	    const char* commandName;
	    const char* description;
	    const char** required;
	    const int required_count;
	    const char** optional;
	    const int optional_count;
	} HelpEntry;
	```
    > If all these structs were mingled together, it would be harder to read them

## Pull Request Denial

- You can have a pull request be denied for improper code styling. This isn't a big deal and isn't meant to hurt feelings.
- If you get a pull request denied for improper code styling, fix it and resubmit your pull request. 

> If you feel you were improperly denied, **cite this document.** This document isn't perfect, there may be errors.
  The reviewer could also improperly interpret something. Be polite. 
> 
> If it's decided that:
> 1. You were wrong.
>     - Fix it and resubmit your pull request.
> 2. The reviewer was wrong.
>     - As long as the rest of the pull request looked good, it will be accepted. 
>     - If there were other issues, it will still be denied until they are fixed.
> 3. This document was wrong.
>     - This document will be properly updated.
>     - Your pull request will be looked at again.