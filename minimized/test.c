// Copyright (c) 2024, "Leo" Dmitry Kuznetsov
// This code and the accompanying materials are made available under the terms
// of BSD-3 license, which accompanies this distribution. The full text of the
// license may be found at https://opensource.org/license/bsd-3-clause

#include "range_coder.h"
#define rc_implementation
#include "range_coder.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct { // in memory io
    uint8_t  data[1024];
    size_t   bytes;    // number of bytes read by io_read()
    size_t   written;  // number of bytes written by io_write()
    uint64_t checksum; // FNV hash
} io;

static void checksum_init(void) {
    io.checksum = 0xCBF29CE484222325uLL; // FNV offset basis
}

static void checksum_append(const uint8_t byte) {
    io.checksum ^= byte;
    io.checksum *= 0x100000001B3; // FNV prime
    io.checksum ^= (io.checksum >> 32);
    io.checksum  = (io.checksum << 7) | (io.checksum >> (64 - 7));
}

static void io_write(struct range_coder* rc, uint8_t b) {
    if (rc->error == 0) {
        if (io.written < countof(io.data)) {
            checksum_append(b);
            io.data[io.written++] = b;
        } else {
            rc->error = rc_err_too_big;
        }
    }
}

static uint8_t io_read(struct range_coder* rc) {
    if (rc->error == 0) {
        if (io.bytes >= io.written) {
            rc->error = rc_err_io;
        } else {
            assert(io.bytes < countof(io.data));
            checksum_append(io.data[io.bytes]);
            return io.data[io.bytes++];
        }
    }
    return 0;
}

static int32_t compare(const uint8_t in[], const uint8_t out[],
                       size_t n, uint64_t ecs) {
    bool equal = ecs == io.checksum;
    if (!equal) {
        printf("checksum encoder: %016llX != decoder: %016llX\n",
                ecs, io.checksum);
    } else {
        for (size_t i = 0; i < n; i++) {
            if (in[i] != out[i]) {
                printf("[%d]: %d != %d\n", (int)i, in[i], out[i]);
                equal = false;
                break;
            }
        }
    }
    assert(equal); // break early for debugging
    return equal && ecs == io.checksum ? 0 : rc_err_data;
}

static uint64_t encode(struct prob_model*  pm, struct range_coder* rc,
                       const uint8_t a[], size_t n, uint32_t symbols) {
    pm_init(pm, symbols);
    rc_init(rc, 0);
    for (size_t i = 0; i < n && rc->error == 0; i++) {
        rc_encode(rc, pm, a[i]);
    }
    rc_flush(rc);
    assert(rc->error == 0);
    return io.checksum;
}

static size_t decode(struct prob_model*  pm, struct range_coder* rc,
                     uint8_t a[], size_t n, uint32_t symbols) {
    io.bytes = 0;
    checksum_init();
    pm_init(pm, symbols);
    rc->code = 0;
    for (size_t i = 0; i < sizeof(rc->code); i++) {
        rc->code = (rc->code << 8) + rc->read(rc);
    }
    rc_init(rc, rc->code);
    size_t i = 0;
    while (i < n && rc->error == 0) {
        uint8_t  sym = rc_decode(rc, pm);
        a[i++] = (uint8_t)sym;
    }
    return i;
}

static double entropy(const uint64_t a[], size_t n) {
    double total = 0;
    for (size_t i = 0; i < n; i++) {
        if (a[i] > 1) { total += a[i]; }
    }
    double e = 0;
    for (size_t i = 0; i < n; i++) {
        if (a[i] > 1) {
            double p = a[i] / total;
            e -= p * log2(p);
        }
    }
    return e;
}

int main(void) {
    static struct range_coder  coder;
    static struct prob_model   model;
    struct range_coder* rc = &coder;
    struct prob_model*  pm = &model;
    static const char text[] =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit, "
        "sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
        "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris "
        "nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in "
        "reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla "
        "pariatur. Excepteur sint occaecat cupidatat non proident, sunt in "
        "culpa qui officia deserunt mollit anim id est laborum.";
    enum { bits = 8 };
    enum { symbols = 1 << bits };
    enum { n = countof(text) - 1 }; // last byte text[countof(text)] == 0
    io.bytes = 0;
    io.written = 0;
    rc->write = io_write;
    rc->read = io_read;
    checksum_init();
    const uint8_t* in = (const uint8_t*)text;
    uint64_t ecs = encode(pm, rc, in, n, symbols);
    const double e = entropy(pm->freq, symbols);
    const double bps = io.written * 8.0 / n;
    const double percent = 100.0 * io.written * 8 / ((int64_t)n * bits);
    printf("%lld to %lld bytes. %.1f%% bps: %.3f Shannon H: %.3f\n",
           ((uint64_t)n * bits / 8), (uint64_t)io.written, percent, bps, e);
    uint8_t out[n];
    size_t k = decode(pm, rc, out, n, symbols);
    assert(rc->error == 0 && k == n && ecs == io.checksum);
    int32_t r = k == n ? compare(in, out, n, ecs) : rc_err_invalid;
    printf("decode(): %s\n", r == 0 ? "ok" : "failed");
    assert(r == 0);
    return r;
}
