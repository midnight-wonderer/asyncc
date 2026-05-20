#include <stdint.h>
#include <stdio.h>
#include "asyncc.h"

static asyncc_chan_t my_chan;

asyncc asyncc_state_t producer(int limit)
{
    int i;
    asyncc_begin;

    for (i = 0; i < limit; i++) {
        printf("Producer sending: %d\n", i);
        asyncc_chan_write(&my_chan, &i);
        asyncc_yield;
    }

    printf("Producer finished.\n");
    asyncc_end;
}

asyncc asyncc_state_t consumer(void)
{
    int val;
    asyncc_begin;

    while (1) {
        asyncc_chan_read(&my_chan, &val);
        printf("Consumer received: %d\n", val);
        if (val >= 4) {
            break;
        }
        asyncc_yield;
    }

    printf("Consumer finished.\n");
    asyncc_end;
}

int main(void)
{
    asyncc_runner_t runner;
    asyncc_runner_init(&runner);
    asyncc_chan_init(&my_chan);

    static producer_state_t p;
    producer_init(&p, 5);
    asyncc_runner_add(&runner, &p.task, producer_run);

    static consumer_state_t c;
    consumer_init(&c);
    asyncc_runner_add(&runner, &c.task, consumer_run);

    printf("Starting runner...\n");
    while (runner.tasks_head != NULL) {
        asyncc_runner_run_once(&runner);
    }
    printf("Runner finished.\n");
    return 0;
}
