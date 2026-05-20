// @file asyncc.h
// Async for (embedded) C
//
// Copyright (c) 2023 Tom Wolf
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
//
#ifndef ASYNCC_H
#define ASYNCC_H

#define ASYNCC_VERSION_MAJOR    0
#define ASYNCC_VERSION_MINOR    0
#define ASYNCC_VERSION_PATCH    1

// This magic is used to allow a nice declaration of local variables in the
// ASYNC_BEGIN() macro (up to 10 locals, which is a reasonable limit and
// can be easily extended with some modifications)
#define FE_0(ACTION)
#define FE_1(ACTION, X) ACTION(X) 
#define FE_2(ACTION, X, ...) ACTION(X)FE_1(ACTION, __VA_ARGS__)
#define FE_3(ACTION, X, ...) ACTION(X)FE_2(ACTION, __VA_ARGS__)
#define FE_4(ACTION, X, ...) ACTION(X)FE_3(ACTION, __VA_ARGS__)
#define FE_5(ACTION, X, ...) ACTION(X)FE_4(ACTION, __VA_ARGS__)
#define FE_6(ACTION, X, ...) ACTION(X)FE_5(ACTION, __VA_ARGS__)
#define FE_7(ACTION, X, ...) ACTION(X)FE_6(ACTION, __VA_ARGS__)
#define FE_8(ACTION, X, ...) ACTION(X)FE_7(ACTION, __VA_ARGS__)
#define FE_9(ACTION, X, ...) ACTION(X)FE_8(ACTION, __VA_ARGS__)
#define FE_10(ACTION, X, ...) ACTION(X)FE_9(ACTION, __VA_ARGS__)

#define GET_MACRO(_0,_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,NAME,...) NAME 
#define FOR_EACH(ACTION,...) \
  GET_MACRO(_0,__VA_ARGS__,FE_10,FE_9,FE_8,FE_7,FE_6,FE_5,FE_4,FE_3,FE_2,FE_1,FE_0)(ACTION,__VA_ARGS__)

// Some actions
#define L_DEFINE(X)     X;
#define L_DEFINES(...) FOR_EACH(L_DEFINE,__VA_ARGS__)

enum async {
    ASYNC_INIT,
    ASYNC_CONT = ASYNC_INIT,
    ASYNC_ERR,
    ASYNC_DONE,
};

// IDEA: provide a #define to disable all stack bounds checking (scary)

// Simplest way to provide local state is to expand it to a local struct, point
// it to the top of the stack, and advance the stack index by sizeof(struct)

#ifdef LIVE_DANGEROUSLY

// Init stack index and initial spot within function (no len, live dangerously)
#define async_init(s, len)              \
        *((uint16_t*)s+0) = 2;          \
        *((uint16_t*)s+1) = ASYNC_INIT

#define ASYNC_BEGIN(s, ...)                                         \
    uint16_t *s_idx = (uint16_t*)(s);                               \
    struct locals { L_DEFINES(uint16_t spot, __VA_ARGS__) } *l;     \
    l = (struct locals*)(s + *s_idx);                               \
    *s_idx += sizeof(struct locals);                                \
    switch (l->spot) { default:

#define async_begin(s, ...) ASYNC_BEGIN(s, __VA_ARGS__)

#else

// Init stack index, max length, and initial spot within function
#define async_init(s, len)              \
        *((uint16_t*)s+0) = 4;          \
        *((uint16_t*)s+1) = len;        \
        *((uint16_t*)s+2) = ASYNC_INIT

#define async_begin(s, ...)                                         \
    uint16_t *s_idx = (uint16_t*)(s);                               \
    uint16_t *s_max = (uint16_t*)(s)+1;                             \
    struct locals { L_DEFINES(uint16_t spot, __VA_ARGS__) } *l;     \
    if ((*s_idx + sizeof(struct locals)) > *s_max) {                \
        async_err(s, sizeof(struct locals));                        \
        return ASYNC_ERR;                                           \
    } else {                                                        \
        l = (struct locals*)(s + *s_idx);                           \
        a_push();                                                   \
        switch (l->spot) { default:

#define ASYNC_BEGIN(s, ...) async_begin(s, __VA_ARGS__)

#endif

#define a_push() *s_idx+=sizeof(struct locals)
#define a_pop()  *s_idx-=sizeof(struct locals)

#define async_end(s) case ASYNC_DONE: a_pop(); return ASYNC_DONE; } }

#define await_while(cond) l->spot = __LINE__; case __LINE__:if (cond) { a_pop(); return ASYNC_CONT; }
#define await(cond) await_while(!(cond))


#define async_yield l->spot = __LINE__; a_pop(); return ASYNC_CONT; case __LINE__:
#define async_exit l->spot = ASYNC_DONE; a_pop(); return ASYNC_DONE

// For those who don't like dereferencing struct members so much:
#define _(v) l->v

// Helpers to get stack index, max len, and current spot
#define IDX(s)  *((uint16_t*)s+0)

#ifdef LIVE_DANGEROUSLY
#define SPOT(s) *((uint16_t*)s+1)
#else
#define MAX(s)  *((uint16_t*)s+1)
#define SPOT(s) *((uint16_t*)s+2)
#endif

#define async_done(s) SPOT(s) = ASYNC_DONE
#define async_is_done(s) (SPOT(s) == ASYNC_DONE)
#define async_call(f, s) (async_is_done(s) || (f)(s))

// Gets the 8-bit stack value for printing
#define SVAL(s,idx) *((uint8_t*)s+idx)

// Print the contents of the stack (for debugging)
#define print_stack(s)                      \
    printf("STACKDUMP: %s(): line %d\n", __FUNCTION__, __LINE__);   \
    printf("  STACK: %s (%p)\n", #s, s);                            \
    printf("  IDX: 0x%04X (%d),  SIZE: 0x%04X (%d)\n",              \
            IDX(s), IDX(s), MAX(s), MAX(s));                        \
    printf("  SPOT: %d \n", SPOT(s));                               \
    printf("    MEMORY: | ");                                       \
    for (int i=0; i<0x10; i++) {                                    \
        printf("0x%02X ", i);                                       \
    }                                                               \
    printf("\n    ");                                               \
    for (int i=0; i<0x11; i++) {                                    \
        printf("-----");                                            \
    }                                                               \
    printf("\n");                                                   \
    for (int row=0; (row * 0x10) < MAX(s); row++) {                 \
        printf("    0x%04X: | ", row*0x10);                         \
        for (int i=row*0x10; (i<row*0x10+0x10) && i<MAX(s); i++) {  \
            printf("0x%02X ", SVAL(s,i));                           \
        }                                                           \
        printf("\n");                                               \
    }
    


// Counting semaphores support (ported from async.h)
struct async_sem {
    unsigned int count;
};

#define async_sem_init(sem, val) (sem)->count = (val)
#define await_sem(sem)           \
    do {                         \
        await((sem)->count > 0); \
        --(sem)->count;          \
    } while (0)
#define async_sem_signal(sem)    ++(sem)->count

#endif // ASYNCC_H
