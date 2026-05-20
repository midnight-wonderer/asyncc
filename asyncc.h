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
#define ASYNCC_VERSION_MINOR    2
#define ASYNCC_VERSION_PATCH    3

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
    void *woken_by; // Reference to the channel/event that woke this task up
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
    task->woken_by = NULL;
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
    void *buffer;
    size_t elem_size;
    uint16_t cap;
    uint16_t size;
    uint16_t head;
    uint16_t tail;
} asyncc_chan_t;

static inline void asyncc_chan_init(asyncc_chan_t *chan) {
    chan->waiting_writer = NULL;
    chan->waiting_reader = NULL;
    chan->data_ptr = NULL;
    chan->buffer = NULL;
    chan->elem_size = 0;
    chan->cap = 0;
    chan->size = 0;
    chan->head = 0;
    chan->tail = 0;
}

static inline void asyncc_chan_init_buffered(asyncc_chan_t *chan, void *buffer, size_t elem_size, uint16_t cap) {
    chan->waiting_writer = NULL;
    chan->waiting_reader = NULL;
    chan->data_ptr = NULL;
    chan->buffer = buffer;
    chan->elem_size = elem_size;
    chan->cap = cap;
    chan->size = 0;
    chan->head = 0;
    chan->tail = 0;
}

#define asyncc_chan_write(chan_ptr, val_ptr) \
    do { \
        asyncc_chan_t *_c = (chan_ptr); \
        bool _blocked = false; \
        if (_c->waiting_reader != NULL && _c->waiting_reader->blocked) { \
            memcpy(_c->data_ptr, val_ptr, _c->cap > 0 ? _c->elem_size : sizeof(*(val_ptr))); \
            asyncc_task_t *_reader = _c->waiting_reader; \
            _c->waiting_reader = NULL; \
            _reader->blocked = false; \
            _reader->woken_by = _c; \
        } else if (_c->cap > 0 && _c->size < _c->cap) { \
            unsigned char *_buf = (unsigned char *)_c->buffer; \
            memcpy(_buf + (_c->tail * _c->elem_size), val_ptr, _c->elem_size); \
            _c->tail = (_c->tail + 1) % _c->cap; \
            _c->size++; \
        } else { \
            _c->waiting_writer = (asyncc_task_t*)l; \
            _c->data_ptr = (void*)(val_ptr); \
            l->task.blocked = true; \
            _blocked = true; \
        } \
        if (_blocked) { \
            asyncc_yield; \
        } \
    } while (0)

#define asyncc_chan_read(chan_ptr, val_ptr) \
    do { \
        asyncc_chan_t *_c = (chan_ptr); \
        bool _blocked = false; \
        if (_c->cap > 0 && _c->size > 0) { \
            unsigned char *_buf = (unsigned char *)_c->buffer; \
            memcpy(val_ptr, _buf + (_c->head * _c->elem_size), _c->elem_size); \
            _c->head = (_c->head + 1) % _c->cap; \
            _c->size--; \
            if (_c->waiting_writer != NULL && _c->waiting_writer->blocked) { \
                unsigned char *_buf_w = (unsigned char *)_c->buffer; \
                memcpy(_buf_w + (_c->tail * _c->elem_size), _c->data_ptr, _c->elem_size); \
                _c->tail = (_c->tail + 1) % _c->cap; \
                _c->size++; \
                asyncc_task_t *_writer = _c->waiting_writer; \
                _c->waiting_writer = NULL; \
                _writer->blocked = false; \
                _writer->woken_by = _c; \
            } \
        } else if (_c->waiting_writer != NULL && _c->waiting_writer->blocked) { \
            memcpy(val_ptr, _c->data_ptr, _c->cap > 0 ? _c->elem_size : sizeof(*(val_ptr))); \
            asyncc_task_t *_writer = _c->waiting_writer; \
            _c->waiting_writer = NULL; \
            _writer->blocked = false; \
            _writer->woken_by = _c; \
        } else { \
            _c->waiting_reader = (asyncc_task_t*)l; \
            _c->data_ptr = (void*)(val_ptr); \
            l->task.blocked = true; \
            _blocked = true; \
        } \
        if (_blocked) { \
            asyncc_yield; \
        } \
    } while (0)

// Non-blocking try operations
static inline bool asyncc_chan_try_read(asyncc_chan_t *chan, void *val_ptr, size_t val_size) {
    if (chan->cap > 0 && chan->size > 0) {
        unsigned char *buf = (unsigned char *)chan->buffer;
        memcpy(val_ptr, buf + (chan->head * chan->elem_size), chan->elem_size);
        chan->head = (chan->head + 1) % chan->cap;
        chan->size--;
        if (chan->waiting_writer != NULL && chan->waiting_writer->blocked) {
            unsigned char *buf_w = (unsigned char *)chan->buffer;
            memcpy(buf_w + (chan->tail * chan->elem_size), chan->data_ptr, chan->elem_size);
            chan->tail = (chan->tail + 1) % chan->cap;
            chan->size++;
            asyncc_task_t *writer = chan->waiting_writer;
            chan->waiting_writer = NULL;
            writer->blocked = false;
            writer->woken_by = chan;
        }
        return true;
    } else if (chan->waiting_writer != NULL && chan->waiting_writer->blocked) {
        memcpy(val_ptr, chan->data_ptr, val_size);
        asyncc_task_t *writer = chan->waiting_writer;
        chan->waiting_writer = NULL;
        writer->blocked = false;
        writer->woken_by = chan;
        return true;
    }
    return false;
}

static inline bool asyncc_chan_try_write(asyncc_chan_t *chan, void *val_ptr, size_t val_size) {
    if (chan->waiting_reader != NULL && chan->waiting_reader->blocked) {
        memcpy(chan->data_ptr, val_ptr, val_size);
        asyncc_task_t *reader = chan->waiting_reader;
        chan->waiting_reader = NULL;
        reader->blocked = false;
        reader->woken_by = chan;
        return true;
    } else if (chan->cap > 0 && chan->size < chan->cap) {
        unsigned char *buf = (unsigned char *)chan->buffer;
        memcpy(buf + (chan->tail * chan->elem_size), val_ptr, chan->elem_size);
        chan->tail = (chan->tail + 1) % chan->cap;
        chan->size++;
        return true;
    }
    return false;
}

// -------------------------------------------------------------
// Select Case (Waiting on multiple channels)
// -------------------------------------------------------------
#define asyncc_select_read2(ch1, val1, ch2, val2) \
    do { \
        asyncc_chan_t *_c1 = (ch1); \
        asyncc_chan_t *_c2 = (ch2); \
        if (asyncc_chan_try_read(_c1, val1, sizeof(*(val1)))) { \
            l->task.woken_by = _c1; \
        } else if (asyncc_chan_try_read(_c2, val2, sizeof(*(val2)))) { \
            l->task.woken_by = _c2; \
        } else { \
            _c1->waiting_reader = (asyncc_task_t*)l; \
            _c1->data_ptr = (void*)(val1); \
            _c2->waiting_reader = (asyncc_task_t*)l; \
            _c2->data_ptr = (void*)(val2); \
            l->task.blocked = true; \
            asyncc_yield; \
            if (l->task.woken_by == _c1) { \
                _c2->waiting_reader = NULL; \
            } else if (l->task.woken_by == _c2) { \
                _c1->waiting_reader = NULL; \
            } else { \
                _c1->waiting_reader = NULL; \
                _c2->waiting_reader = NULL; \
            } \
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
