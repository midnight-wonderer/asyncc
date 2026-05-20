# asyncc - Async & CSP for Embedded C

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](#)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A lightweight, zero-allocation coroutine and Communicating Sequential Processes (CSP) library for C, optimized for resource-constrained embedded systems and cooperative multitasking environments. 

`asyncc` gives you a modern, readable `async`/`await` programming experience in C without the high memory footprint of multi-threaded operating systems (RTOS) or the developer overhead of hand-crafted state machines.

---

## 📖 Table of Contents

- [Why asyncc?](#-why-asyncc)
- [Key Features](#-key-features)
- [Project Documentation](#-project-documentation)
- [Quick Start](#-quick-start)
- [Limitations & Rules](#-limitations--rules)
- [License](#-license)

---

## ⚡ Why asyncc?

Developing event-driven code on embedded systems usually means choosing between two extremes:
* **RTOS Threads**: Easy to write blockingly, but require a separate stack per thread (often 256 bytes to several kilobytes of RAM each), which scales poorly.
* **State Machines**: Highly memory-efficient, but complex to maintain, nest, and refactor.

`asyncc` occupies the middle ground. It uses stackless coroutines where multiple tasks run cooperatively in a single scheduler thread. To solve the classic protothreads limitation (where local variables do not persist across yields), `asyncc` uses a compile-time Python preprocessor to automatically extract variables and construct persistent state structures.

---

## 🌟 Key Features

* **Zero-Allocation**: No `malloc` or heap usage. All structures (task states, channels, queues) are statically allocated.
* **Persistent Local Variables**: Write functions that look like synchronous code; the preprocessor automatically generates matching state structs.
* **Synchronous & Buffered CSP Channels**: Blockingly or non-blockingly pass messages directly between coroutines via memory copies.
* **N-way Select Multiplexing**: Wait on up to 5 channels simultaneously (similar to Go's `select` statement) without race conditions.
* **Thread Gates**: Bridges that safely connect asynchronous coroutines with OS threads and Interrupt Service Routines (ISRs).

---

## 📚 Project Documentation

To learn more, check out the dedicated documentation files:

* 🚀 **[Getting Started Guide](docs/getting_started.md)**: Installation, dependencies (`pycparser`), build system integration (`xmake`, `Makefile`), and a simple Hello World example.
* 🛠️ **[Features & API Reference](docs/features.md)**: Comprehensive details on task runner loops, buffered/unbuffered channels, select statements, and thread gates.
* ⚠️ **[Limitations & Pitfalls](docs/limitations.md)**: Mandatory rules concerning local variable declarations, nested switch-case limits, and single-producer/single-consumer rules.

---

## 🏁 Quick Start

### 1. Requirements
Ensure you have Python 3 and the `pycparser` package installed to run the preprocessor:
```bash
pip install pycparser
```

### 2. Write a Coroutine (`demo.asyncc.c`)
Write your coroutines using the `asyncc` keyword and define local variables before `asyncc_begin`:

```c
#include <stdio.h>
#include "asyncc.h"

static asyncc_chan_t channel;
static int channel_buffer[2];

asyncc asyncc_state_t sender(void)
{
    int val = 100;
    asyncc_begin;

    printf("Sender: Writing value to channel...\n");
    asyncc_chan_write(&channel, &val);

    asyncc_end;
}

asyncc asyncc_state_t receiver(void)
{
    int received;
    asyncc_begin;

    asyncc_chan_read(&channel, &received);
    printf("Receiver: Got value: %d\n", received);

    asyncc_end;
}

int main(void)
{
    asyncc_runner_t runner;
    asyncc_runner_init(&runner);
    asyncc_chan_init_buffered(&channel, channel_buffer, sizeof(int), 2);

    static sender_state_t snd;
    static receiver_state_t rcv;

    sender_init(&snd);
    receiver_init(&rcv);

    asyncc_runner_add(&runner, &snd.task, sender_run);
    asyncc_runner_add(&runner, &rcv.task, receiver_run);

    while (runner.tasks_head != NULL) {
        asyncc_runner_run_once(&runner);
    }
    return 0;
}
```

### 3. Preprocess and Compile
Before passing the code to a C compiler, run the preprocessor:
```bash
python3 asyncc_preprocess.py demo.asyncc.c demo.c
gcc demo.c -I. -o demo
./demo
```

---

## ⚠️ Limitations & Rules

Due to the lightweight nature of stackless coroutines, please keep the following rules in mind:
1. **No yields inside local switch statements**: Yielding/awaiting inside custom `switch` blocks is forbidden. Use `if-else` chains instead.
2. **Local variable declaration**: All variables that must retain their state across yields must be declared *before* `asyncc_begin`.
3. **Single-Producer Single-Consumer**: Channels support exactly one concurrent reader and one concurrent writer. Multiple concurrent readers/writers will cause lost wakeups.

For detailed explanations of these limitations and workarounds, read the **[Limitations & Pitfalls Guide](docs/limitations.md)**.

---

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
