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

void init_state(
    struct state * restrict const me,
    const struct geometry * const geometry);

struct state * create_state(const struct geometry * const geometry);
void destroy_state(struct state * restrict const me);

static inline int state_status(const struct state * const me)
{
    return me->next != 0 ? 0 : me->active ^ 3;
}

static inline bb_t state_get_steps(const struct state * const me)
{
    return me->next;
}

int state_step(struct state * restrict const me, const int step);



struct step_stat
{
    int square;
    int32_t qgames;
    int32_t score;
};

struct ai_explanation
{
    size_t qstats;
    const struct step_stat * stats;
    double time;
    double score;
};

enum param_type
{
    NO_TYPE=0,
    I32,
    U32,
    F32,
    QPARAM_TYPES
};

extern const size_t param_sizes[QPARAM_TYPES];


struct ai_param
{
    const char * name;
    const void * value;
    enum param_type type;
    size_t offset;
};

struct ai
{
    void * data;
    struct state state;
    const char * error;

    int (*reset)(
        struct ai * restrict const ai,
        const struct geometry * const geometry);

    int (*do_step)(
        struct ai * restrict const ai,
        const int step);

    int (*do_steps)(
        struct ai * restrict const ai,
        const unsigned int qsteps,
        const int steps[]);

    int (*undo_step)(struct ai * restrict const ai);
    int (*undo_steps)(struct ai * restrict const ai, const unsigned int qsteps);

    int (*go)(
        struct ai * restrict const ai,
		struct ai_explanation * restrict const explanation);

    const struct ai_param * (*get_params)(const struct ai * const ai);

    int (*set_param)(
        struct ai * restrict const ai,
        const char * const name,
        const void * const value);

    const struct state * (*get_state)(const struct ai * const ai);

    void (*free)(struct ai * restrict const ai);
};

static inline const struct state * ai_get_state(const struct ai * const ai)
{
    return &ai->state;
}

int init_random_ai(
    struct ai * restrict const ai,
    const struct geometry * const geometry);

#endif
