#ifndef YOO__VIRUS_WAR__H__
#define YOO__VIRUS_WAR__H__

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
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
};

struct geometry * create_std_geometry(const int n);
void destroy_geometry(struct geometry * restrict const me);



struct state
{
    const struct geometry * geometry;
    int active;
    bb_t x, o, dead;
};

struct state * create_state(const struct geometry * const geometry);
void destroy_state(struct state * restrict const me);

#endif
