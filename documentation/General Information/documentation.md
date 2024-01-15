# Code Documentation Guidelines

> Just to preface this, I know you hate writing docs. I do too. Regardless, it's important.

Documentation is key to collaboration. 
I want to create a welcoming community and take away some of the complexities of OS development.

## Exposed Constructs

Constructs that are exposed and can be called from headers should be documented using Doxygen comments.
- This includes things such as namespaces, classes, functions, etc.
- C-style structs do not necessarily have to be commented as long as they aren't [too complex](#code-complexity).
> As a former Java developer, I heavily prefer the `@tag` style than the `\\tag` style. 
> Please make all doxygen comments in this style. 

Examples:
 - Functions:
    ```c
    /**
     * @brief Brief description of the function.
     *
     * Detailed description of the function
     * (Not necessary if the brief covers everything.)
     *
     * @param param1 Description of parameter 1.
     * @param param2 Description of parameter 2.
     * @return Description of the return value.
     */
    void exposedFunction(int param1, int param2);
    ```
 - Classes:
    ```C++
    /**
     * @brief Brief description of the class.
     *
     * Detailed description of the class, including member variables
     * and public methods, if applicable.
     */
    class ExposedClass {
    public:
        /**
         * @brief Constructor with specific initialization.
         */
        ExposedClass();
    
        /* Public methods are the same as external functions. */
        void publicMethod(int param1, int param2);
    };
    ```
   - Constructors, public methods, and destructors are required. Private methods follow the same guidelines as [internal functions](#internal-constructs) 
 - Namespaces:
    ```C++
    /**
     * @brief Brief description of the namespace.
     *
     * Detailed description of the namespace, including its contents
     * and usage guidelines, if applicable.
     */
    namespace ExposedNamespace {
        // Namespace contents...
    } 
    ```
### Requirements
There are a lot of tags in Doxygen. There are only a few requirements:
- `@brief` should be on every function where a doxygen comment is required.
- All `@param` tags must be there if a function takes parameters.
- All `@return` tags must be there if a function returns something.
- A more detailed description is appreciated, but only required for [complex code.](#code-complexity)
  - It's also not required if the brief covers everything, and the internals are well commented.
- Any other tags can be added if needed (such as `@deprecated`, `@ref`, `@todo`, etc.).
## Internal Constructs
For constructs that are meant to be used only within the same source file, Doxygen comments are **_not_** mandatory (but are appreciated).
However, if the constructs' logic is complex or its behavior is not immediately obvious, add comments for clarity.

Examples:
- Functions:
    ```c
    // Internal function within the source file.
    void internalFunction() {
        // Internal comments explaining behavior
    }
    ```
- Classes:
    ```C++
    // Internal class for specific functionality.
    class InternalClass {
    private:
        // Internal descriptions, commments, etc...
    };
    ```
- Namespaces are treated almost exactly like classes in regard to internal constructs.

## Code Complexity
Regardless of the scope of the function there should be documentation whenever the code complexity warrants it.
- Complex code, especially when undocumented, is daunting for anyone wanting to contribute to the project.
- OS development is one of the hardest branches of programming. It's already complicated enough for beginners.

For example, this can be found in a part of the physical allocator:
```C
// We want the start address to be on a 2MB boundary.
uintptr_t old_start_addr = start_address;
start_address = (start_address + 0x1FFFFF) & ~0x1FFFFF; // Round up & clear the lower 21 bits 
length = length - (start_address - old_start_addr); // Adjust length to start at the new boundary
```
- Without comments, someone trying to figure out what this does would spend an unnecessary amount of time on it.

### Breaking up your code
Comments are useful for breaking up chunks of code. This is *highly* encouraged. Just don't do it obsessively.
- Look at other examples already in the kernel.
- Comments used to break up code can come in pretty much any form, just don't make it unnecessarily large.
  - This falls under [comment clarity.](#comment-clarity)
> Pull request that don't document complex code will be denied. 
> What seems straightforward to one contributor might be challenging for others.
> Don't be afraid to comment your code, then resubmit your pull request.
> 
> Any denials for documentation are final, do not argue.

## Comment Clarity
While documenting functions, please ensure that the comments provide meaningful information.
  - Avoid overly simplistic descriptions:
    ```C
    /**
     * @brief function_name
     */
    void function_name();
    ```
    - The only case this allowed is if the function name **perfectly** describes what the function does.
      - `printLogo()` could simply have a brief of `@brief Prints the logo.`
      - Do **not** just copy/paste the function name. `@brief printLogo` is not acceptable.
  - Avoid useless comments inside functions:
    ```C
    // Print install timer
    printf("Install Timer at %dHz\n", frequency_ms);
    ```
> In the same light as [code complexity](#code-complexity), your pull request can be denied for poor comment clarity.
> If denied in this way, it will be explained what is poorly commented.
> Don't be afraid to re-comment your code, then resubmit your pull request.
>
> Any denials for documentation are final, do not argue.