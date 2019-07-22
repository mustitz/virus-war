#ifndef YOO__VIRUS_WAR__H__
#define YOO__VIRUS_WAR__H__

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define   BB_ONE            ((bb_t)1)
#define   BB_SQUARE(sq)     (BB_ONE << (sq))

#define   ACTIVE_X          1
#define   ACTIVE_O          2



void * multialloc(
    const size_t n,
    const size_t * const sizes,
    void * restrict * ptrs,
    const size_t granularity);



typedef __uint128_t bb_t;

static inline int pop_count(const bb_t bb)
{
    const uint64_t lo = bb;
    const uint64_t hi = bb >> 64;
    return __builtin_popcountll(lo) + __builtin_popcountll(hi);
}

static inline bb_t lshift(const bb_t a, int c)
{
    return a << c;
}

static inline bb_t rshift(const bb_t a, int c)
{
    return a >> c;
}



struct geometry
{
    int n;
    bb_t lside, rside, all;
    bb_t x_first_step;
    bb_t o_first_step;
};

struct geometry * create_std_geometry(const int n);
void destroy_geometry(struct geometry * restrict const me);



struct state
{
    const struct geometry * geometry;
    int active;
    bb_t x, o, dead;
    bb_t next;
};

struct state * create_state(const struct geometry * const geometry);
void destroy_state(struct state * restrict const me);

static inline bb_t state_get_steps(const struct state * const me)
{
    return me->next;
}

int state_step(struct state * restrict const me, const int step);

#endif
