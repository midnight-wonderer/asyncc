// @file example1.c
// A basic example to demonstrate the functionality
//
// Copyright (c) 2024 Tom Wolf
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

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "../asyncc.h"

#define DOING(x) printf("Doing: \"%s\"\n", #x); x
#undef print_stack
#define print_stack(x)

uint16_t error_state;
void asyncc_err(uint8_t *s, uint16_t locals_size)
{
    printf("Error: %d\n", locals_size);
}

enum asyncc_state bfunc(uint8_t *s, int repeat)
{
    print_stack(s);
    asyncc_begin(s, uint16_t i);
    print_stack(s);

    // _() macro is an option if you don't like l-> syntax for locals
    for(_(i)=0; _(i)<repeat; _(i)++) {
        printf("bfunc %d: %d\n", repeat, _(i));
        asyncc_yield;
    }

    asyncc_end(s);
}

enum asyncc_state afunc(uint8_t *s)
{
    print_stack(s);
    asyncc_begin(s, uint16_t i, uint8_t s1[3][8]);
    _(i) = 42;
    print_stack(s);

    asyncc_init(l->s1[0], 8);
    asyncc_init(l->s1[1], 8);
    asyncc_init(l->s1[2], 8);
    print_stack(l->s1[0]);
    print_stack(l->s1[1]);
    print_stack(l->s1[2]);

    print_stack(s);

    DOING(await(bfunc(l->s1[0], 1) & bfunc(l->s1[1], 2) & bfunc(l->s1[2], 3)));

    asyncc_end(s);
}

int main(void)
{
    uint8_t stack[64];
    uint8_t *s = stack;

    asyncc_init(s, 64);
    print_stack(s);

    enum asyncc_state status = ASYNCC_INIT;
    while (status != ASYNCC_DONE) {
        DOING(status = afunc(s));
    }

    printf("Done!\n");
}
