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

static_assert(sizeof(int) >= 4, "tests are only for 32/64 bit platforms");

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
    for (int8_t i = 0; i < sizeof(rc->code); i++) {
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

static uint8_t  data[1024 * 1024 * 1024]; // 1GB buffer for compressed output
static size_t   bytes;    // number of bytes read by read_byte()
static size_t   written;  // number of bytes written by write_byte()
static uint64_t checksum; // FNV hash

static void checksum_init(void) {
    checksum = 0xCBF29CE484222325;
}

static void checksum_append(const uint8_t byte) {
    checksum ^= byte;
    checksum *= 0x100000001B3; // FNV prime
    checksum ^= (checksum >> 32);
    checksum  = (checksum << 7) | (checksum >> (64 - 7));
}

static void write_byte(struct range_coder* rc, uint8_t b) {
    if (rc->error == 0) {
        if (written < sizeof(data)) {
            checksum_append(b);
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
            checksum_append(data[bytes]);
            return data[bytes++];
        } else {
            rc->error = E2BIG;
        }
    }
    return 0;
}

static void init_write(struct range_coder* rc) {
    checksum_init();
    written = 0;
    rc->write = write_byte;
}

static void init_read(struct range_coder* rc) {
    checksum_init();
    bytes = 0;
    rc->read = read_byte;
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

static int32_t rc_compare(uint8_t input[], uint8_t output[],
                          size_t n, uint64_t ecs) {
    if (ecs != checksum) {
        printf("checksum encoder: %016llX != decoder: %016llX\n",
                ecs, checksum);
    }
    bool equal = memcmp(input, output, n) == 0;
    if (!equal) {
        for (size_t i = 0; i < n; i++) {
            if (input[i] != output[i]) {
                printf("[%d]: %d != %d\n", (int)i, input[i], output[i]);
                break;
            }
        }
    }
    assert(equal);
    return equal && ecs == checksum ? 0 : rc_err_data;
}

static int32_t rc_test0(void) {
    static uint8_t input[2];
    for (int32_t i = 0; i < countof(input); i++) { input[i]  = (uint8_t)i; }
    {
        pm_init(pm, 2); // probability model
        init_write(rc);
        rc_encoder(rc, pm, input, sizeof(input));
        printf("%d\n", (int)written);
        assert(rc->error == 0);
    }
    uint64_t ecs = checksum; // encoder check sum
    static uint8_t output[countof(input)];
    {
        pm_init(pm, 2); // probability model
        init_read(rc);
        size_t k = rc_decoder(rc, pm, output, sizeof(output), 1); // eom == 1
        printf("%d from %d\n", k, (int)bytes);
        assert(rc->error == 0 && k == countof(input));
    }
    return rc_compare(input, output, sizeof(input), ecs);
}

static int32_t rc_test1(void) {
    static uint8_t input[1024 + 1];
    for (int32_t i = 0; i < countof(input) - 1; i++) {
        input[i]  = i % 255;
    }
    input[countof(input) - 1] = 0xFFu; // EOM end of message
    {
        pm_init(pm, 256);
        init_write(rc);
        rc_encoder(rc, pm, input, sizeof(input));
        assert(rc->error == 0);
    }
    uint64_t ecs = checksum; // encoder check sum
    static uint8_t output[countof(input)];
    {
        pm_init(pm, 256);
        init_read(rc);
        size_t k = rc_decoder(rc, pm, output, sizeof(output), 0xFF);
        printf("%d from %d\n", k, (int)bytes);
        assert(rc->error == 0 && k == countof(input));
    }
    return rc_compare(input, output, sizeof(input), ecs);
}

static int32_t rc_test2(void) {
    // https://en.wikipedia.org/wiki/Lucas_number
    enum { bits = 5 };
    size_t lucas[1u << bits];
    lucas[0] = 2;
    lucas[1] = 1;
    size_t count = lucas[0] + lucas[1];
    for (int32_t i = 2; i < countof(lucas); i++) {
        lucas[i] = lucas[i - 1] + lucas[i - 2];
        count += lucas[i];
    }
    // sum(Lucas(0), ..., Lucas(31)) = 7881195
    static uint8_t input[7881195];
    assert(count <= countof(input));
    int32_t ix = 0;
    for (size_t i = 2; i < countof(lucas); i++) {
        for (size_t j = 0; j < lucas[i]; j++) {
            input[ix++] = (uint8_t)(i % countof(lucas));
        }
    }
    shuffle(input, count);
    {
        pm_init(pm, countof(lucas));
        init_write(rc);
        rc_encoder(rc, pm, input, count);
        assert(rc->error == 0);
    }
    uint64_t ecs = checksum; // encoder check sum
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
        pm_init(pm, countof(lucas));
        init_read(rc);
        size_t k = rc_decoder(rc, pm, output, count, -1);
        swear(rc->error == 0 && k == count);
    }
    return rc_compare(input, output, count, ecs);
}

static int32_t rc_test3(void) { // fuzzing corrupted stream
    static uint8_t input[256];
    for (int8_t i = 0; i < countof(input); i++) { input[i] = i; }
    {
        pm_init(pm, 256);
        init_write(rc);
        rc_encoder(rc, pm, input, countof(input));
        assert(rc->error == 0);
    }
    uint64_t ecs = checksum; // encoder check sum
    static uint8_t output[countof(input)];
    for (int32_t i = 0; i < 64 * 1024; i++) {
        int32_t ix  = (int32_t)(written * rand64(&seed));
        uint8_t bad = (uint8_t)(256 * rand64(&seed));
        if ((data[ix] ^ bad) != data[ix]) {
            data[ix] = data[ix] ^ bad;
            pm_init(pm, 256);
            init_read(rc);
            size_t k = rc_decoder(rc, pm, output, countof(output), 0xFF);
            // Not all data corruption will result in decoder rc->error
            // some bits corruption may result in legitimate data
            // that is decoded in a wrong way. Checking size, checksum
            // and ultimately resulting bits is the remedy.
            // There is no 100% reliable way to ensure that compressed
            // data was not corrupted or intentionally tinkered with.
            if (rc->error != 0) {
                // decoder reported an error
//              printf("error: %d \"%s\"\n", rc->error, strerror(rc->error));
            } else if (k != countof(input)) {
                // length differ
//              printf("error: %d != %d\n", (int)k, (int)countof(input));
            } else {
                bool equal = memcmp(input, output, k) == 0;
                assert(!equal && ecs != checksum);
                printf("equal: %d checksum: %016llX %016llX\n", equal, ecs, checksum);
            }
        }
    }
    return 0;
}

static int32_t rc_test9(void) { // huge 1GB test
    enum { bits = 8 };
    enum { n = 1u << bits };
    const size_t count = 1024 * 1024 * 1024 - 1024;
    uint8_t* input = (uint8_t*)malloc(count);
    if (input == null) { return E2BIG; }
    for (size_t i = 0; i < count; i++) { input[i] = (uint8_t)(i % n); }
    shuffle(input, count);
    {
        pm_init(pm, n);
        init_write(rc);
        rc_encoder(rc, pm, input, count);
        assert(rc->error == 0);
    }
    uint64_t ecs = checksum; // encoder check sum
    {
        double e = entropy(pm, n);
        const double bps = written * 8.0 / count;
        const double percent = 100.0 * written * 8 / ((int64_t)count * bits);
        printf("%lld compressed to %lld bytes. %.1f%% bps: %.3f Shannon H: %.3f\n",
               ((uint64_t)count * bits / 8), (uint64_t)written,
               percent, bps, e);
    }
    uint8_t* output = (uint8_t*)malloc(count); // 4GB
    if (output == null) { free(input); return E2BIG; }
    {
        pm_init(pm, n);
        init_read(rc);
        size_t k = rc_decoder(rc, pm, output, count, -1);
        swear(rc->error == 0 && k == count);
    }
    return rc_compare(input, output, count, ecs);
}

static int32_t rc_tests(bool verbose) {
    (void)verbose;
    return rc_test0() || rc_test1() || rc_test2() || rc_test3() || rc_test9();
}

#endif
