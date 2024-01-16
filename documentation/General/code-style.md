# Code Style
## Table of Contents
- [Indentation](#indentation)
- [Names](#names)
- [Braces](#braces)

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
#### C++
- camelCase
  - If defined as `extern "C"`, and is to be called by C code, it can be snake_case.

### `enum` & `struct`
- Depending on context:
  - PascalCase if name is one word, or makes sense as "object"
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
  - snake_case
    - Every other scenario.

### Union
- All unions should be in snake_case

### Local Variables and Arguments
All local variables and arguments should be camelCase

### Global Variables and `#define`
All global variables and `#define` should be in SCREAMING_SNAKE_CASE
- This includes any macros, excluding the standard library (like assert).

## Braces
The code style for this project is largely based on [K&R](https://en.wikipedia.org/wiki/Indentation_style#K&R_style), particularly the "one true brace style" variant.

### Functions

### Structs

### Classes

### Namespaces

### If/Else & Switch Case

### Labels




