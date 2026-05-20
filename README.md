# asyncc - async for (embedded) C

A lightweight and useful async library for C focused on embedded systems
applications. This is heavily inspired by both Adam Dunkels' Protothreads
library, and Sandro Magi's async.h library (which was also inspired by
Protothreads).  The goal is to give a more authentic async/await experience
in C with minimal overhead (both in memory and developer experience).

# Why This Library?
Existing stackless event-driven libraries for C work great in smaller scopes
where the lack of stack local variables is easily overcome with static function
variables or a few custom async state variables with minimal nesting, but in
more complex projects with many async functions this becomes a non-trivial
exercise that hinders understanding and refactoring, and leads to the
temptation to revert to a more traditional state-machine-based event-driven
architecture in the code (or to take the hit to RAM and use a new thread in
your RTOS).

This library is an attempt to occupy a middle ground between stackless
asynchronous code, and full-blown threads with their own stacks and true
context switching. If you want to avoid the complexity and overhead of RTOS
threads, but also aren't in the mood for hand-crafted state machines in a
custom time-slicing task scheduler or superloop, then this library could be for
you.

# Design Decisions

## Stacks

To implement this middle path approach to blocking code semantics we need a
stack to keep track of the execution context of each async thread, but we also
need it to be portable, scalable, and easy to use.  Also, the main focus of
this library is embedded sysems, so we will be focused on static allocations
throughout the design.

## Batteries-Included

INCOMPLETE

The intent is to provide a few core components that most async applications
will require in order to make it easier to get started.  At this time only the
basic core functionality is provided, and future updates will add things like
an event loop, timer/sleep functionality, and possibly an event polling system
with queue to support common use cases for async code.

A secondary motivation for an opinionated batteries-included approach is to
drive consistency in how async functions are driven and wired together (more
like the runtimes used in other languages with official async support).  In an
embedded system the imagined architecture would be one event loop per thread of
an RTOS, or only a single event loop in a bare metal system.

## Assumptions

This library assumes that it is being used in a resource-constrained embedded
system.  As such many of the features are optional, but we try to pack in as
much as possible into the core functionality (work in progress).

This library assumes a single thread of execution per event loop with no
shared memory access with event loops running in other threads.  Nothing is
stopping you from using RTOS IPC to coordinate async runtimes/event loops if
you so choose.  For embedded systems the use case of spreading async tasks over
a pool of several concurrent threads is largely covered by manually spreading
async tasks across several threads with separate async runtimes.  In theory we
support dynamic spawning of async tasks, but the intended use of this library
is for statically defined embedded systems.

## Magic

This library uses the same switch case macro magic used in Protothreads and
async.h, and there is no intention of using some of the other techniques that
let you use switch cases in async functions.  It is assumed that anyone using
this library will know about the limitations on switch case blocks.

We take the magic to the next level by attempting to handle stack allocations
using macros, but we also aim to keep the code simple and understandable enough
so that it doesn't scare away users due to impossible-to-debug complex macro
magic.

# Examples

NOTE: some of the code below has yet to be implemented, or may be changed if
I find a better way to do the same thing.  The core functionality is all
implemented at this time, but I have a few sketches of ideas here that are
not implemented yet (these are marked in the comments).

You can also see the code in the `examples` folder for code that actually
compiles and runs.  I do not have makefiles yet, but example1.c is simple
enough for make to figure out on its own.


```C
#include "asyncc.h"

enum async example(uint8_t *s)
{
    // Built-in support for defining local stack variables, checks stack
    // depth and calls global error callback on any overflow (handled safely)
    async_begin(s,
        uint8_t i,
        uint8_t buff[8],
        uint8_t s1[8]);

    // Locals available as members of struct pointer l (so l->i, l->buff)
    // A macro _() is defined if you dislike this syntax
    for(l->i=0;_(i)<8;_(i)++) {
        // Easily await other async functions, just pass along the state
        l->buff[l->i] = await(get_byte(s));
    }

    // Note that the fork-join parallelism scenario does require explicit
    // creation of new "sub-stacks" at the forking level, but these can be
    // created in the begin macro like any other "local" variable (see above).
    // Also, note that we only need to allocate a new "sub-stack" for every
    // additional "simultaneous" async thread.
    await(some_func(s) | some_func(l->s1));     // Wait until one completes
    await(some_func(s) & some_func(l->s1));     // Wait until all complete

    // But it may be easier to keep it explicit when playing with parallel
    // sub-functions.

    async_end;
}

// TODO: this is not implemented yet
struct async_runtime runtime;

uint8_t stack[32];

// This callback must be defined somewhere (TODO: not implemented)
void async_error(uint8_t *s);

// A runtime comes with basic timing functionality that needs to be driven
// by a hardware timer ISR or an RTOS timer event of some kind.
// TODO: not implemented
void timer_isr_or_callback(void)
{
    ASYNC_TICK(&runtime, ISR_TICK_IN_MS);
}

int main(void)
{
    // Set up runtime and add an async task to it
    // TODO: async_sched not implemented, see example1.c for how to drive
    // async functions in your own event loop.
    async_init(&runtime);
    async_sched(&runtime, example, &s, stack); // TODO: implement schedule

    // Add multiple tasks, this time using a helper macro
    async_sched(&runtime, example, 32);     // Can run multiple instances
    async_sched(&runtime, example_2, 64);

    // Helper macro to define and init new async_state instances
    ASYNC_STACK(s2, 128);

    for(;;) {
        // Runs next runnable task
        async_next(&runtime);       // TODO: implement

        // You can also manually drive threads if you so choose
        async_call(example, s2);        // TODO: implement
    }
}

```

# Why Shouldn't You Use This Library?

## Limitations vs State Machines

The synchronous/blocking coding style that async and true threads unlock is a
lie.  Explicit event-driven state machines are sometimes the right answer,
especially if the problem is not very linear/sequential.  The event polling
system of this library does make it easy to support explicit state machines,
but it's possible that a library focused on event-driven hierarchical state
machines could be a better fit for some applications.

## It's Unusual

This is an unusual way of programming in C, and using macro magic to make DIY
tiny stacks is probaby a bad idea.  But then again we're writing C code here,
so the risks are already high.

Use this library at your own risk.  I mainly made it just to see if I could ;)

# Ideas

## Overview

This section is a scratch pad for ideas that could find their way into this
library or another new project.

## Stacks all the way down

I had this idea while considering the best way to support the fork-join
parallelism in this library: just use the stack for everything.  This would
also include things like timer support, event support, and other things that
would typically require some fixed fields in the state struct or in the runtime
struct.

This would make it harder to get good estimates of the stack depth required,
but for complex use cases this is already a tough problem without profiling the
code or doing a detailed analysis.  We could clean up the user experience in
other areas by just skimming a few bytes off the bottom of the stack in the
runtime code before the application code is any the wiser.  The user would only
need to provide a stack and initialize it (no more state variables).

One downside to this is that state or runtime variables would be slightly
harder to watch and debug if they are allocated on our stack, but there are a
few half-measures that could make it not horrible (fixed order, or use one
struct for everything that we could possibly need).

I think for fork-join parallelism scenarios this could be enough of a QOL
improvement (half the number of variable declarations) that it is worth the
trade-off.  And some of the downsides can be overcome with some special
debugging tools for inspecting the states on the stack.

UPDATE: migrated the implementation to this style, and it is currently working
quite well.  There is a 32-bit overhead on top of what the async.h library has
for the first-layer async function, but after that the cost is the same if no
local variables are used (16-bytes per nested level).  I also provided a flag
to disable bounds checking of the stack, which then takes it to only 16-bytes
of additional overhead.

## Possible byte savings

If I really want to be clever I could reduce the stack and spot sizes to 10
bits.  That would take us to only 30 bits of overhead to store the index,
length, and spot values (we'd take spot to 12-bits for the free upgrade).

The current size is 48 bits, so it is a meaningful savings of two bytes per
thread.  It would also open up two free bits in the spot values for creative
purposes.

An easier creative switch would be to go to 12-bit stack and length values, and
keep the 16-bit spot values.

## Events on the stack
A function could block for an event by using some stack memory (a byte or
16-bit word) to "watch" for the event.  It does so by calling an event
registering function and then blocking for that memory to be updated with the
value of the awaited event.

I'm not sure what the most sensible implementation of this would be.  It may
make more sense to stick with explicit function calls in the user's code.  Any
magical solution would potentially require more stack space to implement, or
require some other pre-allocated memory to allow an event manager to keep track
of who needs to be informed of events.

## 8-bit spots
Bad idea: we need two labels that we know won't be in 0xFF & __LINE__ for this
to work (for init and done).  The only way I can think of to do this is to
reserve one bit for each of these conditions, which then takes us from 256
lines within a function, to only 64.  That is a much less reasonable
restriction.

We could try to hash the 16-bit line number into 8-bits, but that risks
collisions that would be a nightmare to debug.

Also, standardizing on 16-bits makes the spots far simpler to debug since they
map directly to line numbers.  Also, when testing with an 8-bit local variable
I noticed that the locals struct is padded, so we're probably not losing much
real memory performance anyway.  This could make a 16-bit event monitor local
effectively free, since those bytes would be reserved anyway.
