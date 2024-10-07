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

static void rc_encoder(struct range_coder* rc, struct prob_model * fm,
                       uint8_t data[], size_t count) {
    rc_init(rc, 0);
    for (size_t i = 0; i < count && rc->error == 0; i++) {
        rc_encode(rc, fm, data[i]);
    }
    rc_flush(rc);
}

static size_t rc_decoder(struct range_coder* rc, struct prob_model * fm,
                         uint8_t data[], size_t count, int32_t eom) {
    rc->code = 0;
    for (int i = 0; i < sizeof(rc->code); i++) {
        rc->code = (rc->code << 8) + rc->read(rc);
    }
    rc_init(rc, rc->code);
    size_t i = 0;
    while (i < count && rc->error == 0) {
        uint8_t  sym = rc_decode(rc, fm);
        data[i++] = (uint8_t)sym;
        if (eom >= 0 && sym == (uint8_t)eom) { break; }
    }
    return i;
}

static uint64_t random64(uint64_t* state) {
    // Linear Congruential Generator with inline mixing
    thread_local static bool initialized;
    if (!initialized) { initialized = true; *state |= 1; };
    *state = (*state * 0xD1342543DE82EF95uLL) + 1;
    uint64_t z = *state;
    z = (z ^ (z >> 32)) * 0xDABA0B6EB09322E3uLL;
    z = (z ^ (z >> 32)) * 0xDABA0B6EB09322E3uLL;
    return z ^ (z >> 32);
}

static double rand64(uint64_t *state) { // [0.0..1.0) exclusive to 1.0
    return (double)random64(state) / ((double)UINT64_MAX + 1.0);
}

static uint64_t seed = 1;

static void shuffle(uint8_t a[], size_t n) {
    for (size_t i = 0; i < n; i++) {
        size_t k = (size_t)(n * rand64(&seed));
        size_t j = (size_t)(n * rand64(&seed));
        assert(0 <= k && k < n);
        assert(0 <= j && j < n);
        if (i != j) { swap(a[k], a[j]); }
    }
}

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

static double entropy(struct prob_model* pm, size_t n) {
    assert(pm->tree[countof(pm->tree) - 1] > 0); // total frequency
    double e = 0;
    double total = 0;
    for (size_t i = 0; i < n; i++) {
        assert(pm->freq[i] > 0);
        if (pm->freq[i] > 0) { total += pm->freq[i]; }
    }
    assert(total == pm->tree[countof(pm->tree) - 1]);
    for (size_t i = 0; i < n; i++) {
        assert(pm->freq[i] > 0);
        if (pm->freq[i] > 0) {
            double p = pm->freq[i] / total;
            assert(p < 1.0);
            e -= p * log2(p);
        }
    }
    return e;
}

static struct range_coder  coder;
static struct range_coder* rc = &coder;

static struct prob_model  model;
static struct prob_model* pm = &model;

static int rc_test0(void) {
    rc->write = write_byte;
    rc->read  = read_byte;
    bytes = 0;
    written = 0;
    static uint8_t input[2];
    for (int i = 0; i < countof(input); i++) {
        input[i]  = (uint8_t)i;
        printf("%c\n", 'A' + input[i]);
    }
    {
        pm_init(pm, 2); // probability model
        rc_encoder(rc, pm, input, sizeof(input));
        printf("%d\n", (int)written);
        assert(rc->error == 0);
    }
    static uint8_t output[countof(input)];
    {
        pm_init(pm, 2); // probability model
        size_t k = rc_decoder(rc, pm, output, sizeof(output), 1); // eom == 1
        printf("%d from %d\n", k, (int)bytes);
        assert(rc->error == 0 && k == countof(input));
    }
    assert(memcmp(input, output, sizeof(input)) == 0);
    return memcmp(input, output, sizeof(input));
}

static int rc_test1(void) {
    rc->write = write_byte;
    rc->read  = read_byte;
    bytes = 0;
    written = 0;
    static uint8_t input[1024 + 1];
    for (int i = 0; i < countof(input) - 1; i++) {
        input[i]  = i % 255;
//      printf("%c\n", 'A' + input[i]);
    }
    input[countof(input) - 1] = 0xFFu; // EOM end of message
    {
        pm_init(pm, 256);
        rc_encoder(rc, pm, input, sizeof(input));
        printf("%d\n", (int)written);
        assert(rc->error == 0);
    }
    static uint8_t output[countof(input)];
    {
        pm_init(pm, 256);
        size_t k = rc_decoder(rc, pm, output, sizeof(output), 0xFF);
        printf("%d from %d\n", k, (int)bytes);
        assert(rc->error == 0 && k == countof(input));
    }
    assert(memcmp(input, output, sizeof(input)) == 0);
    return memcmp(input, output, sizeof(input));
}

static int rc_test2(void) {
    // https://en.wikipedia.org/wiki/Lucas_number
    enum { bits = 5 };
    uint64_t lucas[1u << bits];
    lucas[0] = 2;
    lucas[1] = 1;
    uint64_t count = lucas[0] + lucas[1];
    for (int32_t i = 2; i < countof(lucas); i++) {
        lucas[i] = lucas[i - 1] + lucas[i - 2];
        count += lucas[i];
    }
    // sum(Lucas(0), ..., Lucas(31)) = 7881195
    static uint8_t input[7881195];
    assert(count <= countof(input));
    int32_t ix = 0;
    for (int32_t i = 2; i < countof(lucas); i++) {
        for (int32_t j = 0; j < lucas[i]; j++) {
            input[ix++] = (uint8_t)(i % countof(lucas));
        }
    }
    shuffle(input, count);
    {
        rc->write = write_byte;
        written = 0;
        pm_init(pm, countof(lucas));
        rc_encoder(rc, pm, input, count);
        assert(rc->error == 0);
    }
    {
        double e = entropy(pm, countof(lucas));
        const double bps = written * 8.0 / count;
        const double percent = 100.0 * written * 8 / ((int64_t)count * bits);
        printf("%lld compressed to %lld bytes. %.1f%% bps: %.3f Shannon H: %.3f\n",
               ((uint64_t)count * bits / 8), (uint64_t)written,
               percent, bps, e);
    }
    static uint8_t output[countof(input)];
    {
        rc->read  = read_byte;
        bytes = 0;
        pm_init(pm, countof(lucas));
        size_t k = rc_decoder(rc, pm, output, count, -1);
        swear(rc->error == 0 && k == count);
    }
    assert(memcmp(input, output, count) == 0);
    return memcmp(input, output, count);
}

static int rc_test3(void) {
    // test rc_scale_down_freq()
    enum { bits = 2 };
    enum { n = 1u << bits };
    static uint8_t input[n * 1024 * 1024];
    for (int32_t i = 0; i < countof(input); i++) {
        input[i] = (uint8_t)(i % n);
    }
    {
        rc->write = write_byte;
        written = 0;
        pm_init(pm, n);
        for (uint8_t i = 0; i < n; i++) { pm_update(pm, i, pm_max_freq - 1); }
        rc_encoder(rc, pm, input, countof(input));
        assert(rc->error == 0);
    }
    {
        double e = entropy(pm, n);
        const double bps = written * 8.0 / countof(input);
        const double percent = 100.0 * written * 8 / ((int64_t)countof(input) * bits);
        printf("%lld compressed to %lld bytes. %.1f%% bps: %.3f Shannon H: %.3f\n",
               ((uint64_t)countof(input) * bits / 8), (uint64_t)written,
               percent, bps, e);
    }
    static uint8_t output[countof(input)];
    {
        rc->read  = read_byte;
        bytes = 0;
        pm_init(pm, n);
        for (uint8_t i = 0; i < n; i++) { pm_update(pm, i, pm_max_freq - 1); }
        size_t k = rc_decoder(rc, pm, output, countof(input), -1);
        swear(rc->error == 0 && k == countof(input));
    }
    bool equal = memcmp(input, output, countof(input)) == 0;
    if (!equal) {
        for (size_t i = 0; i < countof(input); i++) {
            if (input[i] != output[i]) {
                printf("[%d]: %d != %d\n", (int)i, input[i], output[i]);
            }
        }
    }
    assert(equal);
    return equal;
}


static int rc_tests(bool verbose) {
    (void)verbose;
    return rc_test3();
//  return rc_test0() || rc_test1() || rc_test2() || rc_test3();
}

#endif
