#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "asyncc.h"

static asyncc_gate_t my_gate;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static volatile bool runner_needs_wakeup = false;

// Notification callback invoked by the gate to wake up the runner thread
void gate_notify(void *context)
{
    (void)context;
    pthread_mutex_lock(&mutex);
    runner_needs_wakeup = true;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
}

// Coroutine running inside the runner instance
asyncc asyncc_state_t receiver_task(void)
{
    int val;
    asyncc_begin;

    while (1) {
        printf("Receiver task waiting on gate...\n");
        asyncc_gate_wait(&my_gate, &val);
        printf("Receiver task received: %d\n", val);
        if (val >= 3) {
            break;
        }
        asyncc_yield;
    }

    printf("Receiver task finished.\n");
    asyncc_end;
}

// External native OS thread
void* thread_func(void *arg)
{
    (void)arg;
    for (int val = 1; val <= 3; val++) {
        sleep(1);
        printf("Thread signaling gate with val: %d\n", val);
        asyncc_gate_signal(&my_gate, &val, sizeof(val));
    }
    return NULL;
}

int main(void)
{
    asyncc_runner_t runner;
    asyncc_runner_init(&runner);
    asyncc_gate_init(&my_gate);

    // Register callback so the gate can signal the main thread to wake up
    my_gate.notify_callback = gate_notify;

    static receiver_task_state_t rx;
    receiver_task_init(&rx);
    asyncc_runner_add(&runner, &rx.task, receiver_task_run);

    pthread_t thread;
    pthread_create(&thread, NULL, thread_func, NULL);

    printf("Starting runner loop...\n");
    while (runner.tasks_head != NULL) {
        asyncc_runner_run_once(&runner);
        
        // If there are active tasks, but they are all blocked, suspend the thread
        if (runner.tasks_head != NULL && rx.task.blocked) {
            pthread_mutex_lock(&mutex);
            while (!runner_needs_wakeup) {
                pthread_cond_wait(&cond, &mutex);
            }
            runner_needs_wakeup = false;
            pthread_mutex_unlock(&mutex);
        }
    }
    printf("Runner loop finished.\n");

    pthread_join(thread, NULL);
    return 0;
}
