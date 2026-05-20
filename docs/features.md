# Features and APIs

`asyncc` provides cooperative multitasking features, zero-allocation channels for sequential communication, multiplexing select capabilities, and thread integration gates.

---

## 1. Coroutine Basics & Control

Coroutines in `asyncc` are declared using the `asyncc` modifier and must return `asyncc_state_t`. 

### Coroutine Structure
```c
asyncc asyncc_state_t my_task(int parameter)
{
    // 1. Declarations of stateful local variables
    int loop_counter;
    
    // 2. Coroutine entry point
    asyncc_begin;
    
    // 3. Execution body with yield/await points
    for (loop_counter = 0; loop_counter < parameter; loop_counter++) {
        asyncc_yield;
    }
    
    // 4. Coroutine exit point
    asyncc_end;
}
```

### Control Macros
* **`asyncc_begin`**: Sets up the state-resume switch case statement. Must appear before any executable code that uses yield/await.
* **`asyncc_end`**: Marks the termination of the coroutine. Sets the internal state to `ASYNCC_DONE` and returns `ASYNCC_DONE`.
* **`asyncc_yield`**: Temporarily yields control back to the runner scheduler. The task is rescheduled and will resume execution from the statement immediately following `asyncc_yield` during the next runner tick.
* **`await(condition)`**: Blocks execution of the coroutine until `condition` evaluates to true. Under the hood, this expands to:
  ```c
  await_while(!(condition))
  ```
* **`await_while(condition)`**: Blocks execution of the coroutine while `condition` evaluates to true.

---

## 2. CSP Channels

Channels facilitate communication and synchronization between coroutines. They can be **unbuffered (synchronous)** or **buffered**.

### Unbuffered (Synchronous) Channels
In an unbuffered channel, the writer blocks until a reader is ready to receive, and the reader blocks until a writer is ready to send. Data transfer happens via direct memory copy from writer to reader, requiring zero heap allocation.

* **Initialization**:
  ```c
  asyncc_chan_t my_chan;
  asyncc_chan_init(&my_chan);
  ```
* **Blocking Write / Read**:
  ```c
  int val_to_send = 42;
  asyncc_chan_write(&my_chan, &val_to_send); // Blocks until read
  
  int val_received;
  asyncc_chan_read(&my_chan, &val_received);  // Blocks until written
  ```

### Buffered Channels
A buffered channel contains an internal ring buffer. Writes only block if the buffer is full, and reads only block if the buffer is empty.

* **Initialization**: Requires a statically allocated array for the buffer.
  ```c
  #define BUFFER_CAPACITY 4
  static int channel_buffer[BUFFER_CAPACITY];
  
  asyncc_chan_t buffered_chan;
  asyncc_chan_init_buffered(&buffered_chan, channel_buffer, sizeof(int), BUFFER_CAPACITY);
  ```
* **Blocking Write / Read**: Uses the same `asyncc_chan_write` and `asyncc_chan_read` macros.

### Non-blocking Operations
Non-blocking operations return `true` if they immediately succeed, and `false` otherwise:
* **`asyncc_chan_try_write(chan_ptr, val_ptr, val_size)`**:
  ```c
  int val = 99;
  if (asyncc_chan_try_write(&my_chan, &val, sizeof(val))) {
      printf("Write succeeded immediately!\n");
  }
  ```
* **`asyncc_chan_try_read(chan_ptr, val_ptr, val_size)`**:
  ```c
  int val;
  if (asyncc_chan_try_read(&my_chan, &val, sizeof(val))) {
      printf("Read value: %d\n", val);
  }
  ```

---

## 3. Channel Select (Multiplexing)

`asyncc` supports waiting on multiple channels simultaneously using the preprocessor-driven `asyncc_select_read(...)` macro, which dynamically handles an arbitrary number of channels.

* `asyncc_select_read(ch1, val1, ch2, val2, ...)`

### How It Works
1. It queries each channel non-blockingly. If any channel has data ready, it copies the data, sets `l->task.woken_by` to the pointer of that channel, and continues.
2. If no channel is ready, it registers the current task as the `waiting_reader` on all select channels, blocks the task, and yields.
3. When a writer writes to one of the channels, it unblocks the task and sets its `woken_by` member to the channel's pointer.
4. When the task resumes, it automatically deregisters from all other channels (sets their `waiting_reader` to `NULL`) to avoid race conditions.
5. The calling coroutine checks `l->task.woken_by` to see which channel received the event.

### Example: Multiplexing two channels
```c
static asyncc_chan_t chan_a;
static asyncc_chan_t chan_b;

asyncc asyncc_state_t selector_coroutine(void)
{
    int val_a;
    int val_b;

    asyncc_begin;

    while (1) {
        printf("Waiting for data on chan_a or chan_b...\n");
        asyncc_select_read(&chan_a, &val_a, &chan_b, &val_b);

        if (l->task.woken_by == &chan_a) {
            printf("Received from Chan A: %d\n", val_a);
        } else if (l->task.woken_by == &chan_b) {
            printf("Received from Chan B: %d\n", val_b);
        }
        
        asyncc_yield;
    }

    asyncc_end;
}
```

---

## 4. Thread Gates (Inter-Thread Communication)

`asyncc_gate_t` acts as a thread-safe barrier bridging native OS threads (or Interrupt Service Routines) and `asyncc` coroutines running inside an event loop.

### Signaling from External Threads to Coroutines
A coroutine can block waiting on a gate until an external thread signals it and delivers data.

```c
static asyncc_gate_t my_gate;

// Running in the asyncc runner loop
asyncc asyncc_state_t gate_waiter(void)
{
    int received_val;

    asyncc_begin;
    
    // Blocks the coroutine until external signal
    asyncc_gate_wait(&my_gate, &received_val);
    printf("Gate opened! Received value: %d\n", received_val);

    asyncc_end;
}

// Running in a native OS Thread (e.g. pthread) or ISR
void external_thread_or_isr(void)
{
    int payload = 100;
    // Copies payload and unblocks the waiter task
    asyncc_gate_signal(&my_gate, &payload, sizeof(payload));
}
```

### Sending from Coroutines to External Threads
Conversely, a coroutine can blockingly send data to a gate, waiting for an external thread to retrieve it.

```c
// Running in the asyncc runner loop
asyncc asyncc_state_t gate_sender(void)
{
    int send_val = 500;

    asyncc_begin;
    
    // Sends data and blocks until an external thread retrieves it
    asyncc_gate_send(&my_gate, &send_val);
    printf("Data retrieved by external thread!\n");

    asyncc_end;
}

// Running in a native OS Thread
void external_thread_consumer(void)
{
    int buffer;
    // Non-blocking retrieval check. If successful, unblocks the coroutine
    if (asyncc_gate_retrieve(&my_gate, &buffer, sizeof(buffer))) {
        printf("Consumed value: %d\n", buffer);
    }
}
```

### Wakeup Notifications
To avoid busy-waiting in your runner thread when all tasks are blocked, you can register a notification callback on the gate:

```c
void my_gate_notify(void *context)
{
    // Logic to wake up the main runner loop (e.g., pthread_cond_signal or RTOS task notify)
}

int main(void)
{
    asyncc_gate_init(&my_gate);
    my_gate.notify_callback = my_gate_notify;
    my_gate.notify_context = NULL;
    // ...
}
```
If the runner loop is suspended because all tasks are blocked (e.g., waiting on OS input), `asyncc_gate_signal` or `asyncc_gate_send` will invoke this callback to wake the runner thread.

---

## 5. The Task Runner

The task runner scheduling loop manages registered coroutines and runs them cooperatively.

```c
int main(void)
{
    asyncc_runner_t runner;
    asyncc_runner_init(&runner);

    // Register tasks
    static my_task_state_t state1;
    my_task_init(&state1, 10);
    asyncc_runner_add(&runner, &state1.task, my_task_run);

    // Event Loop
    while (runner.tasks_head != NULL) {
        asyncc_runner_run_once(&runner);
        
        // Custom power-saving logic if all tasks are blocked:
        // if (all_tasks_blocked) {
        //     sleep_or_suspend();
        // }
    }
    return 0;
}
```
* **`asyncc_runner_run_once(runner)`**: Iterates over all active tasks. If a task is not marked as `blocked`, its run function is executed. If a task returns `ASYNCC_DONE`, it is removed from the active scheduling list.
