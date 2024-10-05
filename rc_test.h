#ifndef rc_test_header_included
#define rc_test_header_included

// Copyright (c) 2024, "Leo" Dmitry Kuznetsov
// This code and the accompanying materials are made available under the terms
// of BSD-3 license, which accompanies this distribution. The full text of the
// license may be found at https://opensource.org/license/bsd-3-clause

#include "rc.h"
#define rc_implementation
#include "rc.h"

#include <stdbool.h>
#include <stdio.h>

#ifndef countof
#define countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

static uint8_t  data[1024 * 1024 * 1024];
static size_t   written;
static size_t   bytes; // read

static void write_byte(struct range_coder* rc, uint8_t b) {
    if (rc->error == 0) {
        if (written < sizeof(data)) {
            data[written++] = b;
        } else {
            rc->error = E2BIG;
        }
    }
}

static uint8_t read_byte(struct range_coder* rc) {
    if (rc->error == 0) {
        if (bytes >= written) {
            rc->error = EIO;
        } else if (bytes < sizeof(data)) {
            return data[bytes++];
        } else {
            rc->error = E2BIG;
        }
    }
    return 0;
}

static struct range_coder coder;
static struct range_coder* rc = &coder;

static struct freq_model model;
static struct freq_model* fm = &model;

static int rc_test0(void) {
    rc->write = write_byte;
    rc->read  = read_byte;
    static uint8_t input[1024 + 1];
    for (int i = 0; i < countof(input) - 1; i++) {
        input[i]  = i % 255;
//      printf("%c\n", 'A' + input[i]);
    }
    input[countof(input) - 1] = rc_eom;
    // frequency model:
    memset(fm->freq, 0, sizeof(fm->freq));
    for (size_t i = 0; i < countof(input); i++) {
        fm->freq[input[i]]++;
    }
    fm_init(fm);
    {
        rc_encoder(rc, fm, input, sizeof(input));
        printf("%d\n", (int)written);
    }
    static uint8_t output[countof(input)];
    {
        size_t k = rc_decoder(rc, fm, output, sizeof(output));
        printf("%d from %d\n", k, (int)bytes);
        assert(k == countof(input));
    }
    assert(memcmp(input, output, sizeof(input)) == 0);
    return memcmp(input, output, sizeof(input));
}

static int rc_tests(bool verbose) {
    (void)verbose;
    return rc_test0(); // || rc_test1() || rc_test2();
}

#endif
