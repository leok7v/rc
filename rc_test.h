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

// no "int" 16 bit platform support (for tests):

static_assert(sizeof(int)    >= 4, "tests are only for 32/64 bit platforms");
static_assert(sizeof(size_t) >= 4, "tests are only for 32/64 bit platforms");

static bool rc_verbose;

static void* allocate(size_t size) { // sure fail fast malloc()
    void* p = malloc(size);
    assert(p != null);
    if (p == null) { printf("Fatal: OOM\n"); exit(1); }
    return p;
}

static uint64_t nanoseconds(void) {
    // Returns nanoseconds since the epoch start, Midnight, January 1, 1970.
    // The value will wrap around in the year ~2554.
    struct timespec ts;
    int r = timespec_get(&ts, TIME_UTC);
    swear(r == TIME_UTC);
    return (ts.tv_sec * 1000000000uLL + ts.tv_nsec);
}

static uint64_t seed = 1; // random seed start value (must be odd)

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

#define shuffle_t(t) \
static void shuffle_##t(t a[], size_t n) {      \
    for (size_t i = 0; i < n; i++) {            \
        size_t k = (size_t)(n * rand64(&seed)); \
        size_t j = (size_t)(n * rand64(&seed)); \
        swear(0 <= k && k < n);                 \
        swear(0 <= j && j < n);                 \
        if (k != j) { swap(a[k], a[j]); }       \
    }                                           \
}

shuffle_t(uint8_t)
shuffle_t(uint16_t)
shuffle_t(uint32_t)
shuffle_t(uint64_t)

static inline void shuffle_not_implemented(void) { }

#define shuffle(a, n) _Generic(a[0],        \
    uint8_t:  shuffle_uint8_t,              \
    uint16_t: shuffle_uint16_t,             \
    uint32_t: shuffle_uint32_t,             \
    uint64_t: shuffle_uint64_t,             \
    default:  shuffle_not_implemented)(a, n)

static void rc_encoder(struct range_coder* rc, struct prob_model * fm,
                       const uint8_t data[], size_t count) {
    rc_init(rc, 0);
    for (size_t i = 0; i < count && rc->error == 0; i++) {
        rc_encode(rc, fm, data[i]);
    }
    rc_flush(rc);
}

static size_t rc_decoder(struct range_coder* rc, struct prob_model * fm,
                         uint8_t data[], size_t count, int32_t eom) {
    rc->code = 0;
    for (size_t i = 0; i < sizeof(rc->code); i++) {
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

// Shannon Entropy `H`
// Why "if a[i] > 1"?
// Because adaptive model assumes that all symbol in the
// alphabet that has not been seen yet have frequency 1.
// This is important for encoder and decoder to be able
// to be in sync at the start.

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

struct { // in memory io
    uint8_t* data;
    size_t   c; // capacity
    size_t   bytes;    // number of bytes read by read_byte()
    size_t   written;  // number of bytes written by write_byte()
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
        if (io.written < io.c) {
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
            swear(io.bytes < io.c);
            checksum_append(io.data[io.bytes]);
            return io.data[io.bytes++];
        }
    }
    return 0;
}

static void io_alloc(struct range_coder* rc, size_t capacity) {
    io.data = allocate(capacity);
    io.c = capacity;
    io.bytes = 0;
    io.written = 0;
    rc->write = io_write;
    rc->read = io_read;
    checksum_init();
}

static void io_rewind() {
    checksum_init();
    io.bytes = 0;
}

static void io_free(void) {
    free(io.data);
    memset(&io, 0, sizeof(io));
}

#define rc_stats(n, written, bits) do {                                     \
    const double e = entropy(pm->freq, (1u << bits));                       \
    const double bps = written * 8.0 / n;                                   \
    const double percent = 100.0 * written * 8 / ((int64_t)n * bits);       \
    printf("%lld to %lld bytes. %.1f%% bps: %.3f Shannon H: %.3f\n",        \
            ((uint64_t)n * bits / 8), (uint64_t)written, percent, bps, e);  \
} while (0)

static int32_t rc_cmp(const uint8_t in[], const uint8_t out[],
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

static struct range_coder* rc;
static struct prob_model*  pm;

static uint64_t encode(const uint8_t a[], size_t n, uint32_t symbols) {
    pm_init(pm, symbols);
    rc_encoder(rc, pm, a, n);
    swear(rc->error == 0);
    return io.checksum;
}

static size_t decode(uint8_t a[], size_t n, uint32_t symbols, int32_t eom) {
    io_rewind();
    pm_init(pm, symbols);
    size_t k = rc_decoder(rc, pm, a, n, eom);
    return k;
}

#define rc_enter(s) do {                                 \
    if (rc_verbose) { printf(">%s %s\n", __func__, s); } \
} while (0)

#define rc_exit() do {                              \
    if (rc_verbose) { printf("<%s\n", __func__); }  \
} while (0)

static int32_t rc_test0(void) {
    rc_enter("bin");
    enum { symbols = 2 }; // number of symbols in alphabet
    enum { EOM = 1 };     // End of Message symbol
    enum { n = 2 };       // number of input symbols including EOM
    io_alloc(rc, n * 2 + 8);
    uint8_t in[n];
    for (size_t i = 0; i < n; i++) { in[i]  = (uint8_t)i; }
    uint64_t ecs = encode(in, n, symbols); // encoder check sum
    uint8_t out[n];
    size_t k = decode(out, n, symbols, EOM);
    swear(rc->error == 0 && k == n && ecs == io.checksum);
    int32_t r = rc_cmp(in, out, n, ecs);
    io_free();
    rc_exit();
    return r;
}

static int32_t rc_test1(void) {
    rc_enter("EOM");
    enum { symbols = 256 }; // including EOM end of message
    enum { n = 1024 + 1 };
    io_alloc(rc, n * 2 + 8);
    static uint8_t in[n];
    for (size_t i = 0; i < n - 1; i++) {
        in[i]  = i % (symbols - 1);
    }
    in[n - 1] = symbols - 1; // EOM
    uint64_t ecs = encode(in, n, symbols);
    static uint8_t out[n];
    size_t k = decode(out, n, symbols, symbols - 1);
    swear(rc->error == 0 && k == n && ecs == io.checksum);
    int32_t r = rc_cmp(in, out, n, ecs);
    io_free();
    rc_exit();
    return r;
}

static void rc_fill(uint8_t a[], size_t n, uint64_t freq[], size_t m,
                    int32_t symbols) {
    shuffle(freq, m); // shuffle frequencies of symbols distribution
    size_t ix = 0;
    while (ix < n) {
        for (size_t i = 0; i < m && ix < n; i++) {
            for (size_t j = 0; j < freq[i] && ix < n; j++) {
                a[ix++] = (uint8_t)(i % symbols);
            }
        }
    }
    shuffle(a, n); // shuffle resulting array
}

static int32_t rc_test2(void) {
    rc_enter("Lucas");
    // https://en.wikipedia.org/wiki/Lucas_number
    enum { bits = 5 };
    enum { symbols = 1 << bits };
    enum { n = 7881195 };
    io_alloc(rc, n * 2 + 8);
    // lucas[0] + lucas[0] + ... + lucas[31] = 7,881,195
    uint64_t lucas[symbols] = { 2, 1 };
    for (size_t i = 2; i < countof(lucas); i++) {
        lucas[i] = lucas[i - 1] + lucas[i - 2];
    }
    uint8_t* in = allocate(n);
    rc_fill(in, n, lucas, countof(lucas), symbols);
    uint64_t ecs = encode(in, n, symbols);
    if (rc_verbose) { rc_stats(n, io.written, bits); }
    uint8_t* out = allocate(n);
    size_t k = decode(out, n, symbols, -1); // no EOM
    swear(rc->error == 0 && k == n && ecs == io.checksum);
    int32_t r = rc_cmp(in, out, n, ecs);
    free(out);
    free(in);
    io_free();
    rc_exit();
    return r;
}

static int32_t rc_test3(void) {
    rc_enter("Zipf");
    // https://en.wikipedia.org/wiki/Zipf%27s_law
    enum { bits = 8 };
    enum { symbols = 1 << bits };
    enum { n = 1024 * 1024 };
    io_alloc(rc, n * 2 + 8);
    uint64_t zips[symbols];
    for (size_t i = 0; i < countof(zips); i++) { zips[i] = i + 1; }
    uint8_t* in = allocate(n);
    rc_fill(in, n, zips, countof(zips), symbols);
    uint64_t ecs = encode(in, n, symbols);
    if (rc_verbose) { rc_stats(n, io.written, bits); }
    uint8_t* out = allocate(n);
    size_t k = decode(out, n, symbols, -1);
    swear(rc->error == 0 && k == n && ecs == io.checksum);
    int32_t r = rc_cmp(in, out, n, ecs);
    free(out);
    free(in);
    io_free();
    rc_exit();
    return r;
}

static int32_t rc_test4(void) {
    rc_enter("Lorem ipsum");
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
    io_alloc(rc, n * 2 + 8);
    const uint8_t* in = (const uint8_t*)text;
    uint64_t ecs = encode(in, n, symbols);
    if (rc_verbose) { rc_stats(n, io.written, bits); }
    uint8_t out[n];
    size_t k = decode(out, n, symbols, -1);
    swear(rc->error == 0 && k == n && ecs == io.checksum);
    io_free();
    int32_t r = rc_cmp(in, out, n, ecs);
    rc_exit();
    return r;
}

static int32_t rc_test5(void) {
    rc_enter("Long zeros");
    enum { bits = 2 };
    enum { symbols = 1 << bits };
    enum { eom = symbols - 1 };
    enum { n = 1024 * 1024 };
    io_alloc(rc, n * 2 + 8);
    uint8_t* in = allocate(n);
    memset(in, 0, n - 1);
    for (size_t i = 1; i < n; i += 1024) {
        in[i] = (uint8_t)(rand64(&seed) * (symbols - 1));
    }
    for (size_t i = 1; i <= eom; i++) {
        in[n - 1 - (eom - i)] = (uint8_t)i;
    }
    assert(in[n - 1] == eom);
    uint64_t ecs = encode(in, n, symbols);
    if (rc_verbose) { rc_stats(n, io.written, bits); }
    uint8_t* out = allocate(n);
    size_t k = decode(out, n, symbols, eom);
    swear(rc->error == 0 && k == n && ecs == io.checksum);
    int32_t r = rc_cmp(in, out, n, ecs);
    free(out);
    free(in);
    io_free();
    rc_exit();
    return r;
}

static int32_t rc_test6(void) {
    rc_enter("Multi stream");
    enum { bits = 8 };
    enum { symbols = 1 << bits };
    enum { n = 64 * 1024 };
    uint8_t*  in_text = allocate(n);
    uint16_t* in_size = allocate(n * sizeof(uint16_t));
    uint32_t* in_dist = allocate(n * sizeof(uint32_t));
    for (size_t i = 0; i < n; i++) {
        const double z = 1.0 / (n - i); // Zipf's 1/f
        in_text[i] = (uint8_t) (z * rand64(&seed)  * symbols);
        in_size[i] = (uint16_t)(z * rand64(&seed) * ((double)UINT16_MAX + 1));
        in_dist[i] = (uint32_t)(z * rand64(&seed) * ((double)UINT32_MAX + 1));
    }
    shuffle(in_text, n);
    shuffle(in_size, n);
    shuffle(in_dist, n);
    io_alloc(rc, n * 8 * 2);
    struct prob_model  model_text;
    struct prob_model* pm_text = &model_text;
    struct prob_model  model_size[2];
    struct prob_model* pm_size[2] = {&model_size[0], &model_size[1]};
    struct prob_model  model_dist[4];
    struct prob_model* pm_dist[4] = { &model_dist[0], &model_dist[1],
                                      &model_dist[2], &model_dist[3] };
    // encoder:
    pm_init(pm_text, symbols);
    for (size_t j = 0; j < 2; j++) { pm_init(pm_size[j], symbols); }
    for (size_t j = 0; j < 4; j++) { pm_init(pm_dist[j], symbols); }
    rc_init(rc, 0);
    for (size_t i = 0; i < n; i++) {
        rc_encode(rc, pm_text, in_text[i]);
        for (size_t j = 0; j < 2; j++) {
            rc_encode(rc, pm_size[j], (uint8_t)(in_size[i] >> (j * 8)));
        }
        for (size_t j = 0; j < 4; j++) {
            rc_encode(rc, pm_dist[j], (uint8_t)(in_dist[i] >> (j * 8)));
        }
    }
    rc_flush(rc);
    swear(rc->error == 0);
    uint64_t ecs = io.checksum;
    if (rc_verbose) {
        const double e =
            entropy(pm_text->freq, symbols) +
            entropy(pm_size[0]->freq, symbols) +
            entropy(pm_size[1]->freq, symbols) +
            entropy(pm_dist[0]->freq, symbols) +
            entropy(pm_dist[1]->freq, symbols) +
            entropy(pm_dist[2]->freq, symbols) +
            entropy(pm_dist[3]->freq, symbols);
        const uint64_t in_bits = n * (1 + 2 + 4) * 8;
        const double percent = 100.0 * io.written * 8.0 / in_bits;
        printf("%lld to %lld bytes. %.1f%%\n",
                ((uint64_t)in_bits / 8), (uint64_t)io.written, percent);
        printf("Shannon H: %.3f text: %.3f size: %.3f %.3f dist: %.3f %.3f %.3f %.3f\n",
            e / (1 + 2 + 4),
            entropy(pm_text->freq, symbols),
            entropy(pm_size[0]->freq, symbols),
            entropy(pm_size[1]->freq, symbols),
            entropy(pm_dist[0]->freq, symbols),
            entropy(pm_dist[1]->freq, symbols),
            entropy(pm_dist[2]->freq, symbols),
            entropy(pm_dist[3]->freq, symbols));
    }
    // decoder:
    pm_init(pm_text, symbols);
    for (size_t j = 0; j < 2; j++) { pm_init(pm_size[j], symbols); }
    for (size_t j = 0; j < 4; j++) { pm_init(pm_dist[j], symbols); }
    io_rewind();
    rc->code = 0;
    for (size_t i = 0; i < sizeof(rc->code); i++) {
        rc->code = (rc->code << 8) + rc->read(rc);
    }
    rc_init(rc, rc->code);
    uint8_t*  out_text = allocate(n);
    uint16_t* out_size = allocate(n * sizeof(uint16_t));
    uint32_t* out_dist = allocate(n * sizeof(uint32_t));
    for (size_t i = 0; i < n; i++) {
        out_text[i] = rc_decode(rc, pm_text);
        out_size[i] = 0;
        for (size_t j = 0; j < 2; j++) {
            out_size[i] |= rc_decode(rc, pm_size[j]) << (j * 8);
        }
        out_dist[i] = 0;
        for (size_t j = 0; j < 4; j++) {
            out_dist[i] |= rc_decode(rc, pm_dist[j]) << (j * 8);
        }
    }
    swear(rc->error == 0 && ecs == io.checksum);
    int32_t r = rc_cmp(in_text, out_text, n, ecs);
    swear(r == 0);
    if (memcmp(in_size, out_size, n * sizeof(uint16_t)) != 0) {
        r = rc_err_invalid;
    }
    if (memcmp(in_dist, out_dist, n * sizeof(uint32_t)) != 0) {
        r = rc_err_invalid;
    }
    free(out_dist);
    free(out_size);
    free(out_text);
    free(in_dist);
    free(in_size);
    free(in_text);
    io_free();
    rc_exit();
    return r;
}

static int32_t rc_test7(void) { // fuzzing: corrupted stream
    // https://en.wikipedia.org/wiki/Fuzzing
    rc_enter("Fuzzing");
    enum { symbols = 256 };
    enum { n = 256 };
    io_alloc(rc, n * 2 + 8);
    uint8_t in[n];
    for (size_t i = 0; i < n; i++) {
        in[i] = (uint8_t)(rand64(&seed) * symbols);
    }
    uint64_t ecs = encode(in, n, symbols);
    uint8_t out[n];
    for (size_t i = 0; i < 9999; i++) {
        int32_t ix  = (int32_t)(io.written * rand64(&seed));
        uint8_t bad = (uint8_t)(rand64(&seed) * symbols);
        if ((io.data[ix] ^ bad) != io.data[ix]) {
            io.data[ix] = io.data[ix] ^ bad;
            size_t k = decode(out, n, symbols, -1);
            // Not all data corruption will result in decoder rc->error
            // some bits corruption may result in legitimate data
            // that is decoded in a wrong way. Checking size, checksum
            // and ultimately resulting bits is the remedy.
            // There is no 100% reliable way to ensure that compressed
            // data was not corrupted or intentionally tinkered with.
            if (rc->error != 0) {
                // decoder reported an error
//              printf("error: %d \"%s\"\n", rc->error, strerror(rc->error));
            } else if (k != n) {
                // length differ
//              printf("error: %d != %d\n", (int)k, (int)n);
            } else {
                bool equal = memcmp(in, out, k) == 0;
                swear(!equal && ecs != io.checksum);
//              printf("equal: %d checksum: %016llX %016llX\n", equal, ecs, checksum);
            }
        }
    }
    io_free();
    rc_exit();
    return 0;
}

static int32_t rc_test8(void) { // huge 1GB test
    int32_t r = 0;
    #ifndef DEBUG // only in release mode, too slow for debug
    rc_enter("Huge");
    enum { bits = 8 };
    enum { symbols = 1u << bits };
    // On Windows x86 malloc( > 1GB) fails
    const size_t n = (sizeof(size_t) == 8 ? 1024 : 512) * (1024 * 1024);
    io_alloc(rc, n * 2 + 8);
    uint8_t* in = allocate(n);
    for (size_t i = 0; i < n; i++) { in[i] = (uint8_t)(i % symbols); }
    shuffle(in, n);
    uint64_t ecs = encode(in, n, symbols);
    if (rc_verbose) { rc_stats(n, io.written, bits); }
    uint8_t* out = allocate(n);
    size_t k = decode(out, n, symbols, -1);
    swear(rc->error == 0 && k == n && ecs == io.checksum);
    r = rc_cmp(in, out, n, ecs);
    free(out);
    free(in);
    io_free();
    rc_exit();
    #endif
    return r;
}

static int32_t rc_tests(int iterations, bool verbose, bool randomize) {
    swear(iterations > 0);
    if (randomize) { seed = nanoseconds() | 1; }
    rc_verbose = verbose;
    // if the tests fail it is useful to know the starting seed value to debug
    printf("seed: 0x%016llX\n", seed); // even in non verbose mode
    // globally used range coder and probability model (except multi stream test)
    rc = allocate(sizeof(struct range_coder));
    pm = allocate(sizeof(struct prob_model));
    int32_t r = 0;
    for (int i = 0; i < iterations && r == 0; i++) {
        r = rc_test0() || rc_test1() || rc_test2() ||
            rc_test3() || rc_test4() || rc_test5() ||
            rc_test6() || rc_test7() || rc_test8();
    }
    free(pm);
    free(rc);
    printf("rc_tests() %s\n", r == 0 ? "OK" : "FAIL");
    return r;
}

#endif // rc_test_header_included
