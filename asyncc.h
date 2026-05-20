// @file asyncc.h
// Zero-allocation coroutines with CSP channels
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
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ASYNCC_VERSION_MAJOR 0
#define ASYNCC_VERSION_MINOR 2
#define ASYNCC_VERSION_PATCH 4

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

void asyncc_runner_add(asyncc_runner_t *runner, asyncc_task_t *task,
                       asyncc_state_t (*run)(asyncc_task_t *));
void asyncc_runner_run_once(asyncc_runner_t *runner);

// Coroutine Control Macros
#define asyncc_begin                                                           \
  switch (l->task.spot) {                                                      \
  default:

#define asyncc_end                                                             \
  l->task.spot = ASYNCC_DONE;                                                  \
  return ASYNCC_DONE;                                                          \
  }

#define asyncc_yield                                                           \
  do {                                                                         \
    l->task.spot = __LINE__;                                                   \
    return ASYNCC_CONT;                                                        \
  case __LINE__:;                                                              \
  } while (0)

#define await_while(cond)                                                      \
  do {                                                                         \
    l->task.spot = __LINE__;                                                   \
  case __LINE__:                                                               \
    if (cond) {                                                                \
      return ASYNCC_CONT;                                                      \
    }                                                                          \
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

static inline void asyncc_chan_init_buffered(asyncc_chan_t *chan, void *buffer,
                                             size_t elem_size, uint16_t cap) {
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

#define asyncc_chan_write(chan_ptr, val_ptr)                                   \
  do {                                                                         \
    asyncc_chan_t *_c = (chan_ptr);                                            \
    bool _blocked = false;                                                     \
    if (_c->waiting_reader != NULL && _c->waiting_reader->blocked) {           \
      memcpy(_c->data_ptr, val_ptr,                                            \
             _c->cap > 0 ? _c->elem_size : sizeof(*(val_ptr)));                \
      asyncc_task_t *_reader = _c->waiting_reader;                             \
      _c->waiting_reader = NULL;                                               \
      _reader->blocked = false;                                                \
      _reader->woken_by = _c;                                                  \
    } else if (_c->cap > 0 && _c->size < _c->cap) {                            \
      unsigned char *_buf = (unsigned char *)_c->buffer;                       \
      memcpy(_buf + (_c->tail * _c->elem_size), val_ptr, _c->elem_size);       \
      _c->tail = (_c->tail + 1) % _c->cap;                                     \
      _c->size++;                                                              \
    } else {                                                                   \
      _c->waiting_writer = (asyncc_task_t *)l;                                 \
      _c->data_ptr = (void *)(val_ptr);                                        \
      l->task.blocked = true;                                                  \
      _blocked = true;                                                         \
    }                                                                          \
    if (_blocked) {                                                            \
      asyncc_yield;                                                            \
    }                                                                          \
  } while (0)

#define asyncc_chan_read(chan_ptr, val_ptr)                                    \
  do {                                                                         \
    asyncc_chan_t *_c = (chan_ptr);                                            \
    bool _blocked = false;                                                     \
    if (_c->cap > 0 && _c->size > 0) {                                         \
      unsigned char *_buf = (unsigned char *)_c->buffer;                       \
      memcpy(val_ptr, _buf + (_c->head * _c->elem_size), _c->elem_size);       \
      _c->head = (_c->head + 1) % _c->cap;                                     \
      _c->size--;                                                              \
      if (_c->waiting_writer != NULL && _c->waiting_writer->blocked) {         \
        unsigned char *_buf_w = (unsigned char *)_c->buffer;                   \
        memcpy(_buf_w + (_c->tail * _c->elem_size), _c->data_ptr,              \
               _c->elem_size);                                                 \
        _c->tail = (_c->tail + 1) % _c->cap;                                   \
        _c->size++;                                                            \
        asyncc_task_t *_writer = _c->waiting_writer;                           \
        _c->waiting_writer = NULL;                                             \
        _writer->blocked = false;                                              \
        _writer->woken_by = _c;                                                \
      }                                                                        \
    } else if (_c->waiting_writer != NULL && _c->waiting_writer->blocked) {    \
      memcpy(val_ptr, _c->data_ptr,                                            \
             _c->cap > 0 ? _c->elem_size : sizeof(*(val_ptr)));                \
      asyncc_task_t *_writer = _c->waiting_writer;                             \
      _c->waiting_writer = NULL;                                               \
      _writer->blocked = false;                                                \
      _writer->woken_by = _c;                                                  \
    } else {                                                                   \
      _c->waiting_reader = (asyncc_task_t *)l;                                 \
      _c->data_ptr = (void *)(val_ptr);                                        \
      l->task.blocked = true;                                                  \
      _blocked = true;                                                         \
    }                                                                          \
    if (_blocked) {                                                            \
      asyncc_yield;                                                            \
    }                                                                          \
  } while (0)

// Non-blocking try operations
bool asyncc_chan_try_read(asyncc_chan_t *chan, void *val_ptr, size_t val_size);
bool asyncc_chan_try_write(asyncc_chan_t *chan, void *val_ptr, size_t val_size);

// -------------------------------------------------------------
// Select Case (Waiting on multiple channels)
// -------------------------------------------------------------
#define asyncc_select_read(...) ((void)0)
#define asyncc_select_read2(...) ((void)0)
#define asyncc_select_read3(...) ((void)0)
#define asyncc_select_read4(...) ((void)0)
#define asyncc_select_read5(...) ((void)0)
#define asyncc_select_read6(...) ((void)0)
#define asyncc_select_read7(...) ((void)0)
#define asyncc_select_read8(...) ((void)0)
#define asyncc_select_read9(...) ((void)0)
#define asyncc_select_read10(...) ((void)0)
#define asyncc_select_read11(...) ((void)0)
#define asyncc_select_read12(...) ((void)0)
#define asyncc_select_read13(...) ((void)0)
#define asyncc_select_read14(...) ((void)0)
#define asyncc_select_read15(...) ((void)0)
#define asyncc_select_read16(...) ((void)0)

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

#define asyncc_gate_wait(gate_ptr, val_ptr)                                    \
  do {                                                                         \
    asyncc_gate_t *_g = (gate_ptr);                                            \
    _g->waiting_task = (asyncc_task_t *)l;                                     \
    _g->data = (void *)(val_ptr);                                              \
    _g->event_triggered = false;                                               \
    l->task.blocked = true;                                                    \
    asyncc_yield;                                                              \
  } while (0)

void asyncc_gate_signal(asyncc_gate_t *gate, void *val_ptr, size_t val_size);

#define asyncc_gate_send(gate_ptr, val_ptr)                                    \
  do {                                                                         \
    asyncc_gate_t *_g = (gate_ptr);                                            \
    _g->waiting_task = (asyncc_task_t *)l;                                     \
    _g->data = (void *)(val_ptr);                                              \
    _g->event_triggered = false;                                               \
    l->task.blocked = true;                                                    \
    if (_g->notify_callback != NULL) {                                         \
      _g->notify_callback(_g->notify_context);                                 \
    }                                                                          \
    asyncc_yield;                                                              \
  } while (0)

bool asyncc_gate_retrieve(asyncc_gate_t *gate, void *val_ptr, size_t val_size);

#endif // ASYNCC_H
