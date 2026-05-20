// @file asyncc.h
// Zero-allocation coroutines with CSP channels
//
// Copyright (c) 2026 Tom Wolf, Antigravity
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

#ifndef ASYNCC_H
#define ASYNCC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define ASYNCC_VERSION_MAJOR    0
#define ASYNCC_VERSION_MINOR    1
#define ASYNCC_VERSION_PATCH    0

// The asyncc decorator for the preprocessor
#define asyncc

typedef enum {
    ASYNCC_INIT,
    ASYNCC_CONT,
    ASYNCC_DONE,
} asyncc_state_t;

struct asyncc_task;

typedef struct asyncc_task {
    asyncc_state_t (*run)(struct asyncc_task *self);
    uint16_t spot;
    struct asyncc_task *next;
    bool blocked;
} asyncc_task_t;

typedef struct {
    asyncc_task_t *tasks_head;
} asyncc_runner_t;

// Runner APIs
static inline void asyncc_runner_init(asyncc_runner_t *runner) {
    runner->tasks_head = NULL;
}

static inline void asyncc_runner_add(asyncc_runner_t *runner, asyncc_task_t *task, asyncc_state_t (*run)(asyncc_task_t*)) {
    task->run = run;
    task->spot = ASYNCC_INIT;
    task->blocked = false;
    task->next = runner->tasks_head;
    runner->tasks_head = task;
}

static inline void asyncc_runner_run_once(asyncc_runner_t *runner) {
    asyncc_task_t **curr = &runner->tasks_head;
    while (*curr != NULL) {
        asyncc_task_t *task = *curr;
        if (!task->blocked) {
            asyncc_state_t state = task->run(task);
            if (state == ASYNCC_DONE) {
                // Remove task from active list
                *curr = task->next;
                continue;
            }
        }
        curr = &(*curr)->next;
    }
}

// Coroutine Control Macros
#define asyncc_begin \
    switch (l->task.spot) { default:

#define asyncc_end \
    l->task.spot = ASYNCC_DONE; return ASYNCC_DONE; }

#define asyncc_yield \
    do { \
        l->task.spot = __LINE__; \
        return ASYNCC_CONT; \
        case __LINE__:; \
    } while (0)

#define await_while(cond) \
    do { \
        l->task.spot = __LINE__; \
        case __LINE__: \
        if (cond) { \
            return ASYNCC_CONT; \
        } \
    } while (0)

#define await(cond) await_while(!(cond))


// -------------------------------------------------------------
// CSP Channels (Intra-Runner Communication)
// -------------------------------------------------------------
typedef struct {
    asyncc_task_t *waiting_writer;
    asyncc_task_t *waiting_reader;
    void *data_ptr;
} asyncc_chan_t;

static inline void asyncc_chan_init(asyncc_chan_t *chan) {
    chan->waiting_writer = NULL;
    chan->waiting_reader = NULL;
    chan->data_ptr = NULL;
}

#define asyncc_chan_write(chan_ptr, val_ptr) \
    do { \
        asyncc_chan_t *_c = (chan_ptr); \
        if (_c->waiting_reader != NULL) { \
            memcpy(_c->data_ptr, val_ptr, sizeof(*(val_ptr))); \
            asyncc_task_t *_reader = _c->waiting_reader; \
            _c->waiting_reader = NULL; \
            _reader->blocked = false; \
        } else { \
            _c->waiting_writer = (asyncc_task_t*)l; \
            _c->data_ptr = (void*)(val_ptr); \
            l->task.blocked = true; \
            asyncc_yield; \
        } \
    } while (0)

#define asyncc_chan_read(chan_ptr, val_ptr) \
    do { \
        asyncc_chan_t *_c = (chan_ptr); \
        if (_c->waiting_writer != NULL) { \
            memcpy(val_ptr, _c->data_ptr, sizeof(*(val_ptr))); \
            asyncc_task_t *_writer = _c->waiting_writer; \
            _c->waiting_writer = NULL; \
            _writer->blocked = false; \
        } else { \
            _c->waiting_reader = (asyncc_task_t*)l; \
            _c->data_ptr = (void*)(val_ptr); \
            l->task.blocked = true; \
            asyncc_yield; \
        } \
    } while (0)


// -------------------------------------------------------------
// Thread Gate (Inter-Thread Communication Integration hooks)
// -------------------------------------------------------------
typedef struct {
    asyncc_task_t *waiting_task;
    volatile bool event_triggered;
    void (*notify_callback)(void *context);
    void *notify_context;
    void *data;
} asyncc_gate_t;

static inline void asyncc_gate_init(asyncc_gate_t *gate) {
    gate->waiting_task = NULL;
    gate->event_triggered = false;
    gate->notify_callback = NULL;
    gate->notify_context = NULL;
    gate->data = NULL;
}

#define asyncc_gate_wait(gate_ptr, val_ptr) \
    do { \
        asyncc_gate_t *_g = (gate_ptr); \
        _g->waiting_task = (asyncc_task_t*)l; \
        _g->data = (void*)(val_ptr); \
        _g->event_triggered = false; \
        l->task.blocked = true; \
        asyncc_yield; \
    } while (0)

static inline void asyncc_gate_signal(asyncc_gate_t *gate, void *val_ptr, size_t val_size) {
    if (gate->data != NULL && val_ptr != NULL) {
        memcpy(gate->data, val_ptr, val_size);
    }
    gate->event_triggered = true;
    asyncc_task_t *task = gate->waiting_task;
    if (task != NULL) {
        task->blocked = false;
        gate->waiting_task = NULL;
    }
    if (gate->notify_callback != NULL) {
        gate->notify_callback(gate->notify_context);
    }
}

#define asyncc_gate_send(gate_ptr, val_ptr) \
    do { \
        asyncc_gate_t *_g = (gate_ptr); \
        _g->waiting_task = (asyncc_task_t*)l; \
        _g->data = (void*)(val_ptr); \
        _g->event_triggered = false; \
        l->task.blocked = true; \
        if (_g->notify_callback != NULL) { \
            _g->notify_callback(_g->notify_context); \
        } \
        asyncc_yield; \
    } while (0)

static inline bool asyncc_gate_retrieve(asyncc_gate_t *gate, void *val_ptr, size_t val_size) {
    if (gate->waiting_task != NULL && gate->data != NULL) {
        if (val_ptr != NULL) {
            memcpy(val_ptr, gate->data, val_size);
        }
        asyncc_task_t *task = gate->waiting_task;
        gate->waiting_task = NULL;
        gate->data = NULL;
        task->blocked = false;
        return true;
    }
    return false;
}

#endif // ASYNCC_H
