#ifndef rc_header_included
#define rc_header_included
#include <stdint.h>

#undef rc_debug
//#define rc_debug

#define rc_sym_bits  8
#define rc_sym_count (1uLL << rc_sym_bits)
#define pm_max_freq  (1uLL << (64 - rc_sym_bits * 2 - 1))
#define ft_max_bits  31

struct prob_model  { // probability model
    uint64_t freq[rc_sym_count];
    uint64_t tree[rc_sym_count]; // Fenwick Tree
};

struct range_coder {
    uint64_t low;
    uint64_t range;
    uint64_t code;
    void    (*write)(struct range_coder*, uint8_t);
    uint8_t (*read)(struct range_coder*);
    uint64_t error; // sticky error (errno_t) from read/write
};

void    pm_init(struct prob_model * fm, uint32_t n); // n <= 256
void    pm_update(struct prob_model * fm, uint8_t sym, uint64_t inc);

// decoder needs first 8 bytes in code

void    rc_init(struct range_coder* rc, uint64_t code);
void    rc_encode(struct range_coder* rc, struct prob_model * fm, uint8_t sym);
uint8_t rc_decode(struct range_coder* rc, struct prob_model * fm);

// it is responsibility of the called to initialize the range_coder

#endif // rc_header_included

#ifdef rc_implementation

#include "unstd.h"

static inline int32_t ft_lsb(int32_t i) { // least significant bit only
    assert(0 < i && i < (1uLL << ft_max_bits)); // 0 will lead to endless loop
    return i & (~i + 1); // (i & -i)
}

static void ft_init(uint64_t tree[], size_t n, uint64_t a[]) {
    assert(2 <= n && n <= (1u << ft_max_bits));
    const int32_t m = (int32_t)n;
    for (int32_t i = 0; i <  m; i++) { tree[i] = a[i]; }
    for (int32_t i = 1; i <= m; i++) {
        int32_t parent = i + ft_lsb(i);
        if (parent <= m) {
            assert(tree[parent - 1] < UINT64_MAX - tree[i - 1]);
            tree[parent - 1] += tree[i - 1];
        }
    }
}

static void ft_update(uint64_t tree[], size_t n, int32_t i, uint64_t inc) {
    assert(2 <= n && n <= (1u << ft_max_bits));
    assert(0 <= i && i < (int32_t)n);
    while (i < (int32_t)n) {
        assert(tree[i] <= UINT64_MAX - inc);
        tree[i] += inc;
        i += ft_lsb(i + 1); // Move to the sibling
    }
}

static uint64_t ft_query(const uint64_t tree[], size_t n, int32_t i) {
    // ft_query() cumulative sum of all a[j] for j < i
    assert(2 <= n && n <= (1u << ft_max_bits));
    uint64_t sum = 0;
    // index can be -1 for elements before first and will return 0
    while (i >= 0) {  // grandparent can be in the tree when parent is not
        if (i < (int32_t)n) {
            sum += tree[i];
        }
        i -= ft_lsb(i + 1); // Clear lsb - move to the parent.
    }
    return sum;
}

static int32_t ft_index_of(uint64_t tree[], size_t n, uint64_t const sum) {
    // returns index 'i' of an element such that sum of all a[j] for j < i
    // is less of equal to sum.
    // returns -1 if sum is less than any element of a[]
    assert(2 <= n && n <= (1u << ft_max_bits));
    assert((n & (n - 1)) == 0); // only works for power 2
    if (sum >= tree[n - 1]) { return (int32_t)(n - 1); }
    uint64_t value = sum;
    uint32_t i = 0;
    uint32_t mask = (uint32_t)(n >> 1);
    while (mask != 0) {
        uint32_t t = i + mask;
        if (t <= n && value >= tree[t - 1]) {
            i = t;
            value -= tree[t - 1];
        }
        mask >>= 1;
    }
    return i == 0 && value < sum ? -1 : (int32_t)(i - 1);
}

#define RC_CHECK_FT // check Fenwick Tree implementation

static uint64_t pm_sum_of(struct prob_model * fm, uint32_t sym) {
    uint64_t s = ft_query(fm->tree, countof(fm->tree), sym - 1);
    #ifdef RC_CHECK_FT
        uint64_t sum = 0;
        for (uint32_t i = 0; i < sym; i++) { sum += fm->freq[i]; }
        swear(sum == s);
    #endif
    return s;
}

static uint64_t pm_total_freq(struct prob_model * fm) {
    uint64_t s = fm->tree[countof(fm->tree) - 1];
    #ifdef RC_CHECK_FT
        uint64_t sum = pm_sum_of(fm, rc_sym_count);
        swear(sum == s);
    #endif
    return s;
}

static uint8_t pm_index_of(struct prob_model * fm, uint64_t sum) {
    int32_t ix = ft_index_of(fm->tree, countof(fm->tree), sum) + 1;
    #ifdef RC_CHECK_FT
        uint8_t i = 0;
        while (sum >= fm->freq[i]) {
            sum -= fm->freq[i];
            assert(i < rc_sym_count - 1);
            i++;
        }
        swear(i == ix);
    #endif
    assert(0 <= ix && ix < rc_sym_count);
    return (uint8_t)ix;
}

void pm_init(struct prob_model * fm, uint32_t n) {
    assert(2 <= n && n <= rc_sym_count);
    for (size_t i = 0; i < countof(fm->freq); i++) {
        fm->freq[i] = i < n ? 1 : 0;
    }
    ft_init(fm->tree, countof(fm->tree), fm->freq);
}

static inline void rc_scale_down_freq(struct prob_model * fm) {
    while (fm->tree[countof(fm->tree) - 1] >= pm_max_freq) {
        printf("total: 0x%016llX\n", fm->tree[countof(fm->tree) - 1]);
        // to prevent overflow
        for (int32_t i = 0; i < countof(fm->freq); i++) {
            fm->freq[i] = (fm->freq[i] + 1) / 2;
        }
        ft_init(fm->tree, countof(fm->tree), fm->freq);
    }
}

void pm_update(struct prob_model * fm, uint8_t sym, uint64_t inc) {
    assert(inc <= pm_max_freq);
    rc_scale_down_freq(fm);
    fm->freq[sym] += inc;
    ft_update(fm->tree, countof(fm->tree), sym, inc);
    rc_scale_down_freq(fm);
}

static void rc_emit(struct range_coder* rc) {
    #ifdef rc_debug
    const uint64_t range = rc->range;
    const uint64_t low = rc->low;
    #endif
    const uint8_t byte = (uint8_t)(rc->low >> 56);
    rc->write(rc, byte);
    rc->low   <<= 8;
    rc->range <<= 8;
    assert(rc->range != 0);
    #ifdef rc_debug
    printf("write(0x%02X) range: 0x%016llX := 0x%016llX low: 0x%016llX := 0x%016llX\n",
        byte, range, rc->range, low, rc->low);
    #endif
}

static inline bool rc_leftmost_byte_is_same(struct range_coder* rc) {
    return (rc->low >> 56) == ((rc->low + rc->range) >> 56);
}

void rc_init(struct range_coder* rc, uint64_t code) {
    rc->low   = 0;
    rc->range = UINT64_MAX;
    rc->code  = code; // for decoder - first 8 bytes of input
    rc->error = 0;
}

void rc_encode(struct range_coder* rc, struct prob_model * fm,
               uint8_t sym) {
    #ifdef rc_debug
    const uint64_t range = rc->range;
    const uint64_t low   = rc->low;
    #endif
    uint64_t total = pm_total_freq(fm);
    uint64_t start = pm_sum_of(fm, sym);
    uint64_t size  = fm->freq[sym];
    assert(fm->freq[sym] > 0);
    // alt:
    // rc->low   += start * rc->range / total;
    // rc->range /= total;
    rc->range /= total;
    rc->low   += start * rc->range;
    rc->range *= size;
    #ifdef rc_debug
    printf("`%c` start: %llu size: %llu "
           "range: 0x%016llX := 0x%016llX "
           "low: 0x%016llX := 0x%016llX total: 0x%016llX\n",
            'A' + sym, start, size, range, rc->range, low, rc->low, total);
    #endif
    while (rc_leftmost_byte_is_same(rc)) { rc_emit(rc); }
    while (rc->range < total) { rc_emit(rc); }
    pm_update(fm, sym, 1);
}

static void rc_flush(struct range_coder* rc) {
    for (int i = 0; i < sizeof(rc->low); i++) {
        rc->range = UINT64_MAX; rc_emit(rc);
    }
}

static void rc_consume(struct range_coder* rc) {
    #ifdef rc_debug
    const uint64_t range = rc->range;
    const uint64_t code  = rc->code;
    const uint64_t low   = rc->low;
    #endif
    const uint8_t byte   = rc->read(rc);
    rc->code    = (rc->code << 8) + byte;
    rc->low   <<= 8;
    rc->range <<= 8;
    assert(rc->range != 0);
    #ifdef rc_debug
    printf("read(): 0x%02X range: 0x%016llX := 0x%016llX "
           "low: 0x%016llX := 0x%016llX "
           "code: 0x%016llX := 0x%016llX\n",
        byte, range, rc->range, low, rc->low, code, rc->code);
    #endif
}

uint8_t rc_decode(struct range_coder* rc, struct prob_model * fm) {
    #ifdef rc_debug
    const uint64_t range = rc->range;
    const uint64_t low   = rc->low;
    #endif
    uint64_t total = pm_total_freq(fm);
    // alt:
//  uint64_t sum   = (rc->code - rc->low) * total / rc->range;
    uint64_t sum   = (rc->code - rc->low) / (rc->range / total);
    uint8_t  sym   = pm_index_of(fm, sum);
    uint64_t start = pm_sum_of(fm, sym);
    uint64_t size  = fm->freq[sym];
    rc->range /= total;
    rc->low   += start * rc->range;
    rc->range *= size;
    #ifdef rc_debug
    printf("`%c` start: %llu size: %llu "
           "range: 0x%016llX := 0x%016llX "
           "low: 0x%016llX := 0x%016llX total: 0x%016llX\n",
            'A' + sym, start, size, range, rc->range, low, rc->low, total);
    #endif
    while (rc_leftmost_byte_is_same(rc)) rc_consume(rc);
    while (rc->range < total) rc_consume(rc);
    pm_update(fm, sym, 1);
    return sym;
}

#endif // rc_implementation
