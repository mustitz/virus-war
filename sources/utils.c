#include "virus-war.h"

#include <string.h>

static inline ptrdiff_t ptr_diff(const void * const a, const void * const b)
{
    const char * const byte_ptr_a = a;
    const char * const byte_ptr_b = b;
    return byte_ptr_b - byte_ptr_a;
}

static inline void * ptr_move(void * const ptr, const ptrdiff_t delta)
{
    char * const byte_ptr = ptr;
    return byte_ptr + delta;
}

void * multialloc(const size_t n, const size_t * const sizes,
    void * restrict * ptrs, const size_t granularity)
{
    if (n == 0) {
        return NULL;
    }

    size_t offsets[n];
    size_t offset = 0;
    for (size_t i=0;;) {
        offsets[i] = offset;
        offset += sizes[i];
        if (++i == n) break;

        const size_t mod = offset % granularity;
        if (mod != 0) {
            offset += granularity - mod;
        }
    }

    void * result = malloc(offset + granularity);

    const ptrdiff_t address = ptr_diff(NULL, result);
    const ptrdiff_t mod = address % granularity;
    const size_t gap = mod == 0 ? 0 : granularity - mod;

    for (size_t i=0; i<n; ++i) {
        ptrs[i] = ptr_move(result, offsets[i] + gap);
    }

    return result;
}

void multiallocator_reset(
    struct multiallocator * restrict const me)
{
    me->used_blocks = 0;
    const size_t pointers_sz = me->max_blocks * sizeof(void *);
    for (unsigned int i=0; i<me->qtypes; ++i) {
        struct multiallocator_type * restrict const type = me->types + i;
        type->counter = 0;
        memset(type->pointers, 0, pointers_sz);
    }
}

struct multiallocator * create_multiallocator(
    const size_t max_blocks,
    const size_t block_sz,
    const unsigned int qtypes,
    const size_t type_sizes[])
{
    for (unsigned int i=0; i<qtypes; ++i) {
        if (type_sizes[i] > block_sz) {
            errno = EINVAL;
            return NULL;
        }
    }

    const unsigned int n = qtypes + 3;
    size_t sizes[n];
    void * ptrs[n];

    unsigned int index = 0;
    sizes[index++] = sizeof(struct multiallocator);
    sizes[index++] = qtypes * sizeof(struct multiallocator_type);
    const size_t pointers_sz = max_blocks * sizeof(void*);
    sizes[index++] = pointers_sz;
    for (unsigned int i=0; i<qtypes; ++i) {
        sizes[index++] = pointers_sz;
    }

    void * data = multialloc(n, sizes, ptrs, 64);

    index = 0;
    struct multiallocator * me = ptrs[index++];
    struct multiallocator_type * const types = ptrs[index++];
    void * * blocks = ptrs[index++];
    memset(blocks, 0, pointers_sz);
    for (unsigned int i=0; i<qtypes; ++i) {
        struct multiallocator_type * restrict const type = types + i;
        type->pointers = ptrs[index++];
        type->sz = type_sizes[i];
        type->qitems = block_sz / type->sz;
    }

    me->data = data;
    me->blocks = blocks;
    me->max_blocks = max_blocks;
    me->used_blocks = 0;
    me->block_sz = block_sz;
    me->qtypes = qtypes;
    me->types = types;

    multiallocator_reset(me);

    return me;
}

void destroy_multiallocator(
    struct multiallocator * restrict const me)
{
    const size_t max_blocks = me->max_blocks;
    for (size_t i=0; i<max_blocks; ++i) {
        void * const ptr = me->blocks[i];
        if (ptr != NULL) {
            free(ptr);
        }
    }

    free(me->data);
}

static void * get_block(
    struct multiallocator * restrict const me,
    size_t index)
{
    if (index >= me->max_blocks) {
        return NULL;
    }

    if (me->blocks[index] != NULL) {
        return me->blocks[index];
    }

    void * ptr = malloc(me->block_sz);
    if (ptr == NULL) {
        return NULL;
    }

    me->blocks[index] = ptr;
    return ptr;
}

size_t multiallocator_alloc(
    struct multiallocator * restrict const me,
    const int itype)
{
    struct multiallocator_type * restrict const type = me->types + itype;
    const size_t block = type->counter / type->qitems;
    if (type->pointers[block] != NULL) {
        return type->counter++;
    }

    void * ptr = get_block(me, me->used_blocks);
    if (ptr == NULL) {
        return BAD_ALLOC_INDEX;
    }

    ++me->used_blocks;
    type->pointers[block] = ptr;
    return type->counter++;
}



#ifdef MAKE_CHECK

#include "insider.h"

#define N 4
#define GRANULARITY 64

void test_fail(const char * const fmt, ...) __attribute__ ((format (printf, 1, 2)));

int test_multialloc(void)
{
    const char * base = "0aAK";

    static const size_t sizes[N] = {8000, 301, 5002, 503 };
    void * ptrs[N];
    void * const data = multialloc(N, sizes, ptrs, GRANULARITY);

    if (data == NULL) {
        test_fail("Not enought memory to allocate multiblock.");
    }

    /* Fill data */
    for (int i=0; i<N; ++i) {
        char * restrict const ptr = ptrs[i];
        for (size_t j=0; j<sizes[i]; ++j) {
            ptr[j] = base[i] + (j % 10);
        }
    }

    /* Check allligment */
    for (int i=0; i<N; ++i) {
        const void * const ptr = ptrs[i];
        const ptrdiff_t address = ptr_diff(NULL, ptr);
        if (address % GRANULARITY != 0) {
            test_fail("Returned pointer %p is not alligned to %d (index %d).", ptr, GRANULARITY, i);
        }
    }

    /* Check data (overlapping, out of malloc with valgrind) */
    for (int i=0; i<N; ++i) {
        const char * const ptr = ptrs[i];
        for (size_t j=0; j<sizes[i]; ++j) {
            char expected = base[i] + (j % 10);
            if (ptr[j] != expected) {
                test_fail("Unexpected character “%c” in block #%d:\n"
                          "Expected value is “%c”.",
                          ptr[j], i, expected);
            }
        }
    }

    free(data);
    return 0;
}

void check_popcount(const char * const title, const bb_t value, const int expected)
{
    const int actual = pop_count(value);
    if (actual != expected) {
        test_fail("Failed pop_count(%s), expected %d, actual %d.", title, expected, actual);
    }
}

int test_popcount(void)
{
    const bb_t zero = 0;
    const bb_t one = BB_ONE;
    const bb_t all = ~zero;
    const bb_t one_one = BB_ONE | BB_SQUARE(66);

    check_popcount("zero", zero, 0);
    check_popcount("one", one, 1);
    check_popcount("all", all, 128);
    check_popcount("oneone", one_one, 2);
    return 0;
}

static void check_first_one(const char * const title, const int * ones)
{
    const int expected = ones[0];

    bb_t value = 0;
    for (; *ones >= 0; ++ones) {
        value |= BB_SQUARE(*ones);
    }

    const int result = first_one(value);
    if (result != expected) {
        test_fail("Failed test “%s”: result = %d, expected = %d.", title, result, expected);
    }
}

int test_first_one(void)
{
    const int test1[] = { 0, -1 };
    check_first_one("one", test1);

    const int test2[] = { 1, -1 };
    check_first_one("two", test2);

    const int test3[] = { 100, -1 };
    check_first_one("handred", test3);

    const int test4[] = { 7, 14, 22, 100, -1 };
    check_first_one("multiple", test4);

    const int test5[] = { 77, 88, 99, 101, 111, -1 };
    check_first_one("big-multiple", test5);

    const int test6[] = { 42, 64, 65, 127, -1 };
    check_first_one("all", test6);

    return 0;
}

static void check_nth_one_index(const char * const title, const int * const ones)
{
    int qbits = 0;
    bb_t value = 0;
    const int * ptr = ones;
    for (; *ptr >= 0; ++ptr) {
        value |= BB_SQUARE(*ptr);
        ++qbits;
    }

    for (int i=0; i<qbits; ++i) {
        const int result = nth_one_index(value, i);
        const int expected = ones[i];
        if (result != expected) {
            test_fail("Failed test “%s”: i=%d, result = %d, expected = %d.", title, i, result, expected);
        }
    }
}

int test_nth_one_index(void)
{
    bb_t all = 0;

    for (int i=0; i<128; ++i) {
        bb_t value = BB_SQUARE(i);
        all |= value;
        const int result = nth_one_index(value, 0);
        if (result != i) {
            test_fail("Failed test for %d-th single bit, result = %d.", i, result);
        }
    }

    for (int i=0; i<128; ++i) {
        const int result = nth_one_index(all, i);
        if (result != i) {
            test_fail("Failed all bit test for %d-th bit, result = %d.", i, result);
        }
    }

    const int test1[] = { 0, 1, -1 };
    check_nth_one_index("three", test1);

    const int test2[] = { 7, 14, 22, 100, -1 };
    check_nth_one_index("multiple", test2);

    const int test3[] = { 77, 88, 99, 101, 111, -1 };
    check_nth_one_index("big-multiple", test3);

    const int test4[] = { 42, 64, 65, 127, -1 };
    check_nth_one_index("all", test4);

    return 0;
}

#endif
