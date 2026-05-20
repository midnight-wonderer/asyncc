#include <stdint.h>
#include <stdio.h>
#include "asyncc.h"

static asyncc_chan_t ch1;
static asyncc_chan_t ch2;

// Static buffers for channels (no malloc)
static int buf1[2];
static int buf2[2];

asyncc asyncc_state_t sender(void)
{
    int v1 = 10;
    int v2 = 20;
    int v3 = 30;

    asyncc_begin;

    printf("Sender: writing 10 to ch1 (buffered)\n");
    asyncc_chan_write(&ch1, &v1);

    printf("Sender: writing 20 to ch2 (buffered)\n");
    asyncc_chan_write(&ch2, &v2);

    printf("Sender: writing 30 to ch1 (buffered)\n");
    asyncc_chan_write(&ch1, &v3);

    asyncc_end;
}

asyncc asyncc_state_t receiver(void)
{
    int val1;
    int val2;
    int count;

    asyncc_begin;

    for (count = 0; count < 3; count++) {
        printf("Receiver: waiting on select (ch1 or ch2)...\n");
        asyncc_select_read(&ch1, &val1, &ch2, &val2);

        if (l->task.woken_by == &ch1) {
            printf("Receiver: received %d from ch1\n", val1);
        } else if (l->task.woken_by == &ch2) {
            printf("Receiver: received %d from ch2\n", val2);
        }
        
        asyncc_yield;
    }

    asyncc_end;
}

int main(void)
{
    asyncc_runner_t runner;
    asyncc_runner_init(&runner);

    // Initialize buffered channels with static arrays
    asyncc_chan_init_buffered(&ch1, buf1, sizeof(int), 2);
    asyncc_chan_init_buffered(&ch2, buf2, sizeof(int), 2);

    static sender_state_t snd;
    sender_init(&snd);
    asyncc_runner_add(&runner, &snd.task, sender_run);

    static receiver_state_t rcv;
    receiver_init(&rcv);
    asyncc_runner_add(&runner, &rcv.task, receiver_run);

    printf("Starting runner...\n");
    while (runner.tasks_head != NULL) {
        asyncc_runner_run_once(&runner);
    }
    printf("Runner finished.\n");
    return 0;
}
