# Getting Started with asyncc

`asyncc` is a lightweight, zero-allocation coroutine library for C, optimized for resource-constrained embedded systems. It brings a modern, Go-style async/await and Communicating Sequential Processes (CSP) programming model to C by combining Protothreads-style macro state machines with a compile-time Python preprocessor.

---

## Key Concepts

Unlike standard C programs that require heavy RTOS threads (each with its own stack) or complex, manually written state machines, `asyncc` offers:
1. **Stackless Coroutines**: Multiple tasks run cooperatively in a single thread of execution.
2. **Auto-managed State**: A Python preprocessor automatically generates state structures for local variables and function arguments.
3. **CSP Channels**: Statically allocated channels (buffered or unbuffered) for message passing.
4. **Thread Gates**: Bridges to integrate asynchronous runners safely with native OS threads and Interrupt Service Routines (ISRs).

---

## Dependencies & Requirements

To compile and run `asyncc` projects, you need:
- **C Compiler**: Any standard-compliant C99 (or newer) compiler (e.g., GCC, Clang, MSVC).
- **Python 3.x**: Required for the compile-time preprocessor.
- **pycparser**: The preprocessor uses `pycparser` to parse and analyze your C code.

### Installing Python Dependencies

Install `pycparser` using `pip`:
```bash
pip install pycparser
```

---

## How the Preprocessor Works

A major limitation of stackless coroutines in C (like Protothreads) is that standard local variables do not persist across yields. When a function yields, it returns; when it is called again, local variables on the C stack are re-allocated, losing their previous values.

`asyncc` solves this using a Python preprocessor (`asyncc_preprocess.py`) which acts on `.asyncc.c` files:
1. It looks for functions declared with the `asyncc` keyword:
   ```c
   asyncc asyncc_state_t my_coroutine(int param1) {
       int my_local = 0;
       asyncc_begin;
       // ... coroutine body ...
       asyncc_end;
   }
   ```
2. It parses the function parameters and any local variables declared **before** `asyncc_begin`.
3. It generates a state structure containing these variables:
   ```c
   typedef struct {
       asyncc_task_t task;
       int param1;
       int my_local;
   } my_coroutine_state_t;
   ```
4. It generates an initialization function:
   ```c
   static inline void my_coroutine_init(my_coroutine_state_t *l, int param1) {
       l->param1 = param1;
   }
   ```
5. It rewrites the coroutine function to accept the state pointer:
   ```c
   asyncc_state_t my_coroutine_run(asyncc_task_t *self) {
       my_coroutine_state_t *l = (my_coroutine_state_t*)self;
       asyncc_begin;
       // Any references to param1 or my_local are replaced with l->param1 or l->my_local
       asyncc_end;
   }
   ```

> [!IMPORTANT]
> Because of this transformation, you must write your code in files with the `.asyncc.c` extension and run the preprocessor before compiling the generated `.c` files.

---

## Build System Integration

### Using xmake
If you are using `xmake`, you can automatically run the preprocessor by defining a target rule in your `xmake.lua`:

```lua
target("my_app")
    set_kind("binary")
    add_includedirs("path/to/asyncc")
    
    on_load(function (target)
        local projectdir = os.projectdir()
        local gendir = path.join(projectdir, "build", "generated")
        os.mkdir(gendir)
        
        local src_file = path.join(projectdir, "src", "main.asyncc.c")
        local dst_file = path.join(gendir, "main.c")
        
        -- Run the Python preprocessor
        os.runv("python3", {
            path.join(projectdir, "asyncc_preprocess.py"), 
            src_file, 
            dst_file
        })
        target:add("files", dst_file)
    end)
```

### Using a Makefile
Alternatively, you can add a pattern rule to your Makefile:

```makefile
# Rules to build .c from .asyncc.c
build/generated/%.c: src/%.asyncc.c
	@mkdir -p $(@D)
	python3 asyncc_preprocess.py $< $@

# Include the generated files in your source list
SRCS = build/generated/main.c
```

---

## A Simple Example: Co-operative Multitasking

Here is a minimal example (`hello.asyncc.c`) demonstrating how to set up two cooperative coroutines:

```c
#include <stdio.h>
#include "asyncc.h"

// Define a simple coroutine that yields a few times
asyncc asyncc_state_t print_numbers(int limit)
{
    // Declare local variables before asyncc_begin
    int count;

    asyncc_begin;

    for (count = 1; count <= limit; count++) {
        printf("Counter: %d\n", count);
        asyncc_yield; // Hand control back to the runner
    }

    asyncc_end;
}

// Define another coroutine that waits on the first one or does work
asyncc asyncc_state_t ticker(void)
{
    int ticks;

    asyncc_begin;

    for (ticks = 1; ticks <= 3; ticks++) {
        printf("Tick %d...\n", ticks);
        asyncc_yield;
    }

    asyncc_end;
}

int main(void)
{
    asyncc_runner_t runner;
    asyncc_runner_init(&runner);

    // Statically allocate the coroutine state structures
    static print_numbers_state_t task1;
    static ticker_state_t task2;

    // Initialize the states (passing parameters if any)
    print_numbers_init(&task1, 5);
    ticker_init(&task2);

    // Add tasks to the runner
    asyncc_runner_add(&runner, &task1.task, print_numbers_run);
    asyncc_runner_add(&runner, &task2.task, ticker_run);

    printf("Starting Runner Loop...\n");
    // Run the scheduler until all tasks finish
    while (runner.tasks_head != NULL) {
        asyncc_runner_run_once(&runner);
    }
    printf("All tasks completed!\n");

    return 0;
}
```
