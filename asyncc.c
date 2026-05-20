#include "asyncc.h"

void asyncc_runner_add(asyncc_runner_t *runner, asyncc_task_t *task,
                       asyncc_state_t (*run)(asyncc_task_t *)) {
  task->run = run;
  task->spot = ASYNCC_INIT;
  task->blocked = false;
  task->woken_by = NULL;
  task->next = runner->tasks_head;
  runner->tasks_head = task;
}

void asyncc_runner_run_once(asyncc_runner_t *runner) {
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

bool asyncc_chan_try_read(asyncc_chan_t *chan, void *val_ptr, size_t val_size) {
  if (chan->cap > 0 && chan->size > 0) {
    unsigned char *buf = (unsigned char *)chan->buffer;
    memcpy(val_ptr, buf + (chan->head * chan->elem_size), chan->elem_size);
    chan->head = (chan->head + 1) % chan->cap;
    chan->size--;
    if (chan->waiting_writer != NULL && chan->waiting_writer->blocked) {
      unsigned char *buf_w = (unsigned char *)chan->buffer;
      memcpy(buf_w + (chan->tail * chan->elem_size), chan->data_ptr,
             chan->elem_size);
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

bool asyncc_chan_try_write(asyncc_chan_t *chan, void *val_ptr,
                           size_t val_size) {
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

void asyncc_gate_signal(asyncc_gate_t *gate, void *val_ptr, size_t val_size) {
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

bool asyncc_gate_retrieve(asyncc_gate_t *gate, void *val_ptr, size_t val_size) {
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
