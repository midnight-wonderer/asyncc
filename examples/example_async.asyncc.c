#include <stdint.h>
#include <stdio.h>
#include "asyncc.h"

void asyncc_err(uint8_t *s, uint16_t locals_size) {
    (void)s;
    printf("Stack overflow: %d\n", locals_size);
}

asyncc enum asyncc_state bfunc(uint8_t *s, int repeat)
{
    uint16_t i = 0;

    asyncc_begin;

    for (i = 0; i < repeat; i++) {
        printf("bfunc: %d\n", i);
        asyncc_yield;
    }

    asyncc_end;
}

int main(void)
{
    uint8_t stack[64];
    uint8_t *s = stack;

    asyncc_init(s, 64);

    enum asyncc_state status = ASYNCC_INIT;
    while (status != ASYNCC_DONE) {
        status = bfunc(s, 5);
    }

    printf("Done!\n");
    return 0;
}
