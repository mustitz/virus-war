#include "virus-war.h"

struct geometry * create_std_geometry(const int n)
{
    if (n <= 2) {
        errno = EINVAL;
        return NULL;
    }

    const int qsquares = n * n;
    const int qbits = 8 * sizeof(bb_t);
    if (qsquares > qbits) {
        errno = EINVAL;
        return NULL;
    }

    size_t sz = sizeof(struct geometry);
    struct geometry * restrict const me = malloc(sz);
    if (me == NULL) {
        return NULL;
    }

    bb_t left = BB_SQUARE(0);
    bb_t right = BB_SQUARE(n-1);
    bb_t lside = 0;
    bb_t rside = 0;

    for (int i=0; i<n; ++i) {
        lside |= left;
        rside |= right;
        left <<= n;
        right <<= n;
    }

    me->n = n;
    me->lside = lside;
    me->rside = rside;
    me->all = (BB_ONE << qsquares) - 1;
    me->x_first_step = BB_ONE;
    me->o_first_step = BB_SQUARE(qsquares-1);
    return me;
}

void destroy_geometry(struct geometry * restrict const me)
{
    free(me);
}



struct state * create_state(const struct geometry * const geometry)
{
    size_t sz = sizeof(struct state);
    struct state * restrict const me = malloc(sz);
    if (me == NULL) {
        return NULL;
    }

    me->geometry = geometry;
    me->active = ACTIVE_X;
    me->x = 0;
    me->o = 0;
    me->dead = 0;
    me->next = geometry->x_first_step;
    return me;
}

void destroy_state(struct state * restrict const me)
{
    free(me);
}

static bb_t grow(
    const bb_t bb,
    const int n,
    const bb_t all,
    const bb_t not_lside,
    const bb_t not_rside)
{
    const bb_t lbb = (bb & not_lside) >> 1;
    const bb_t rbb = (bb & not_rside) << 1;
    const bb_t hgrow = bb | lbb | rbb;

    const bb_t ubb = lshift(hgrow, n) & all;
    const bb_t dbb = rshift(hgrow, n);
    return hgrow | ubb | dbb;
}

static bb_t next_steps(
    const bb_t my,
    const bb_t opp,
    const bb_t dead,
    const int n,
    const bb_t all,
    const bb_t not_lside,
    const bb_t not_rside)
{
    const bb_t empty = all ^ (my | opp);
    const bb_t my_dead = my & dead;
    bb_t opp_dead = opp & dead;
    bb_t my_live = my ^ my_dead;
    const bb_t opp_live = opp ^ opp_dead;
    const bb_t place = empty | opp_live;

    for (;;) {
        const bb_t cloud = grow(my_live, n, all, not_lside, not_rside);
        const bb_t extra = cloud & opp_dead;
        if (extra == 0) {
            return cloud & place;
        }

        my_live |= extra;
        opp_dead ^= extra;
    }
}

bb_t calc_next_steps(
    const struct state * const me)
{
    const struct geometry * const geometry = me->geometry;
    const int n = geometry->n;
    const bb_t all = geometry->all;
    const bb_t lside = geometry->lside;
    const bb_t rside = geometry->rside;
    const bb_t not_lside = all ^ lside;
    const bb_t not_rside = all ^ rside;

    const bb_t dead = me->dead;
    const bb_t x = me->x;
    const bb_t o = me->o;
    const int all_qsteps = pop_count(x|o) + pop_count(dead);

    if (all_qsteps == 0) {
        return geometry->x_first_step;
    }

    if (all_qsteps == 3) {
        return geometry->o_first_step;
    }

    const int mod = all_qsteps % 3;
    const bb_t my = me->active == ACTIVE_X ? x : o;
    const bb_t opp = me->active == ACTIVE_X ? o : x;

    const bb_t steps = next_steps(my, opp, dead, n, all, not_lside, not_rside);
    if (mod != 0) {
        return steps;
    }

    if (steps == 0) {
        return 0;
    }

    int qsteps = pop_count(steps);
    if (qsteps >= 3) {
        return steps;
    }

    const bb_t killed2 = steps & opp;
    const bb_t expansion2 = steps ^ killed2;
    const bb_t dead2 = dead | killed2;
    const bb_t my2 = my | expansion2;
    const bb_t steps2 = next_steps(my2, opp, dead2, n, all, not_lside, not_rside);

    if (steps2 == 0) {
        return 0;
    }

    qsteps += pop_count(steps2);
    if (qsteps >= 3) {
        return steps;
    }

    const bb_t killed3 = steps2 & opp;
    const bb_t expansion3 = steps2 ^ killed3;
    const bb_t dead3 = dead2 | killed3;
    const bb_t my3 = my2 | expansion3;
    const bb_t steps3 = next_steps(my3, opp, dead3, n, all, not_lside, not_rside);

    qsteps += pop_count(steps3);
    return qsteps >= 3 ? steps : 0;
}



#ifdef MAKE_CHECK

#include "insider.h"

#define  N  9

enum square_placement
{
    UNDEFINED,
    LEFT_SIDE,
    RIGHT_SIDE,
    OUTSIDE
};

void test_square_placement(
    struct geometry * restrict const me,
    const int x,
    const int y,
    enum square_placement square_placement)
{
    const int sq = N*y + x;
    bb_t bb = BB_SQUARE(sq);

    const int lside = (bb & me->lside) != 0;
    const int rside = (bb & me->rside) != 0;
    const int outside = (bb & me->all) == 0;

    switch (square_placement) {

        case LEFT_SIDE:
            if (outside || !lside) {
                test_fail("Board size %d, square %d/%d expected to be on left side.", N, x, y);
            }
            break;

        case RIGHT_SIDE:
            if (outside || !rside) {
                test_fail("Board size %d, square %d/%d expected to be on right side.", N, x, y);
            }
            break;

        case UNDEFINED:
            if (outside || lside || rside) {
                test_fail("Board size %d, square %d/%d expected to be undefined.", N, x, y);
            }
            break;

        case OUTSIDE:
            if (!outside) {
                test_fail("Board size %d, square %d/%d expected to be outside.", N, x, y);
            }
            break;

        default:
            test_fail("Invalid square_placement (%d).", square_placement);
            break;
    }
}

int test_geometry(void)
{
    struct geometry * restrict const me = create_std_geometry(N);
    if (me == NULL) {
        test_fail("create_std_geometry(%d) failed, errno = %d.", N, errno);
    }

    test_square_placement(me,   0,   0, LEFT_SIDE);
    test_square_placement(me,   0, N/2, LEFT_SIDE);
    test_square_placement(me, N-1, N-1, RIGHT_SIDE);
    test_square_placement(me, N-1, N/2, RIGHT_SIDE);
    test_square_placement(me,   1, N-2, UNDEFINED);
    test_square_placement(me, N/2,   1, UNDEFINED);
    test_square_placement(me, N-2,   0, UNDEFINED);
    test_square_placement(me,   0,   N, OUTSIDE);

    destroy_geometry(me);
    return 0;
}

int test_state(void)
{
    struct geometry * restrict const geometry = create_std_geometry(N);
    if (geometry == NULL) {
        test_fail("create_std_geometry(%d) failed, errno = %d.", N, errno);
    }

    struct state * restrict const me = create_state(geometry);
    if (me == NULL) {
        test_fail("create_state(geometry) failed, errno = %d.", errno);
    }

    destroy_state(me);
    destroy_geometry(geometry);
    return 0;
}

static int make_sq(
    const int file,
    const int rank)
{
    return file + rank * N;
}

#define SQ(file, rank) BB_SQUARE(make_sq(file, rank))

int test_grow(void)
{
    struct geometry * restrict const geometry = create_std_geometry(N);
    if (geometry == NULL) {
        test_fail("create_std_geometry(%d) failed, errno = %d.", N, errno);
    }

    const int n = N;
    const bb_t all = geometry->all;
    const bb_t not_lside = all ^ geometry->lside;
    const bb_t not_rside = all ^ geometry->rside;

    const bb_t test1 = 0
        | SQ(1, 1) | SQ(1, 4) | SQ(1, 7)
        | SQ(4, 1) | SQ(4, 4) | SQ(4, 7)
        | SQ(7, 1) | SQ(7, 4) | SQ(7, 7)
        ;

    if (grow(test1, n, all, not_lside, not_rside) != all) {
        test_fail("grow(test1) != all.");
    }

    const bb_t test2 = SQ(0, 5) | SQ(6, 2) | SQ(7, 1) | SQ(8, 0) | SQ(8, 8);

    bb_t expected2 = 0;
    for (int file = 0; file <= 1; ++file)
    for (int rank = 4; rank <= 6; ++rank) {
        expected2 |= SQ(file, rank);
    }

    for (int file = 5; file <= 8; ++file)
    for (int rank = 0; rank <= 3; ++rank) {
        expected2 |= SQ(file, rank);
    }

    expected2 |= SQ(8, 8) | SQ(8, 7) | SQ(7, 7) | SQ(7, 8);

    expected2 ^= SQ(5, 0);
    expected2 ^= SQ(8, 3);

    const bb_t result2 = grow(test2, n, all, not_lside, not_rside);
    if (result2 != expected2) {
        test_fail("grow(test2) != expected2.");
    }

    destroy_geometry(geometry);
    return 0;
}

int test_next_steps(void)
{
    struct geometry * restrict const geometry = create_std_geometry(N);
    if (geometry == NULL) {
        test_fail("create_std_geometry(%d) failed, errno = %d.", N, errno);
    }

    const int n = N;
    const bb_t all = geometry->all;
    const bb_t not_lside = all ^ geometry->lside;
    const bb_t not_rside = all ^ geometry->rside;

    const bb_t my = SQ(8, 0) | SQ(8, 1) | SQ(8, 2);
    const bb_t opp = SQ(7, 1) | SQ(7, 3) | SQ(6, 4) | SQ(4, 6) | SQ(7, 6) | SQ(5, 0);
    const bb_t dead = opp | SQ(8, 1);
    const bb_t steps = next_steps(my, opp, dead, n, all, not_lside, not_rside);

    const bb_t expected = 0
        | SQ(5, 3) | SQ(5, 4) | SQ(5, 5)
        | SQ(6, 0) | SQ(6, 1) | SQ(6, 2) | SQ(6, 3) | SQ(6, 5)
        | SQ(7, 0) | SQ(7, 2) | SQ(7, 4) | SQ(7, 5)
        | SQ(8, 3) | SQ(8, 4)
        ;

    if (steps != expected) {
        test_fail("next_steps(my, opp, opp) != expected");
    }

    destroy_geometry(geometry);
    return 0;
}

struct calc_next_steps_data
{
    const char * title;
    int active;
    bb_t x;
    bb_t o;
    bb_t dead;
    bb_t expected;
};

void test_calc_next_steps_data(
    struct state * restrict const me,
    const struct calc_next_steps_data * const data)
{
    me->active = data->active;
    me->x = data->x;
    me->o = data->o;
    me->dead = data->dead;
    const bb_t result = calc_next_steps(me);
    if (result != data->expected) {
        test_fail("Failed to execute subtest “%s”.", data->title);
    }
}

int test_calc_next_steps(void)
{
    struct geometry * restrict const geometry = create_std_geometry(N);
    if (geometry == NULL) {
        test_fail("create_std_geometry(%d) failed, errno = %d.", N, errno);
    }

    struct state * restrict const me = create_state(geometry);
    if (me == NULL) {
        test_fail("create_state(geometry) failed, errno = %d.", errno);
    }

    const struct calc_next_steps_data data[] = {
        { "First X move", 1, 0, 0, 0, SQ(0,0) },
        { "First O move", 2, SQ(0,0)|SQ(1,1)|SQ(2,2), 0, 0, SQ(8,8) },

        { "Second X move (fold 01, 10)", 1,
            SQ(0,0)|SQ(0,1)|SQ(1,0),
            0,
            SQ(0,1)|SQ(1,0),
            SQ(1,1) },

        { "No moves", 1,
            SQ(0,0)|SQ(0,1)|SQ(1,0)|SQ(1,1),
            0,
            SQ(0,1)|SQ(1,0)|SQ(1,1),
            0 },

        { "Second X move", 1,
            SQ(0,0),
            0,
            0,
            SQ(0,1)|SQ(1,0)|SQ(1,1) },

        { "No second move", 1,
            SQ(0,0)|SQ(0,2)|SQ(1,2)|SQ(1,1)|SQ(2,1)|SQ(2,0),
            SQ(8,8),
            SQ(0,2)|SQ(1,2)|SQ(1,1)|SQ(2,1)|SQ(2,0),
            0 },

         { "Second good", 1,
            SQ(0,0)|SQ(0,1)|SQ(1,1),
            SQ(8,8),
            SQ(0,1)|SQ(1,1),
            SQ(1,0) },

        { "No third move", 1,
            SQ(0,0)|SQ(0,1)|SQ(1,1)|SQ(2,1)|SQ(3,0)|SQ(3,1),
            SQ(8,8),
            SQ(0,1)|SQ(1,1)|SQ(2,1)|SQ(3,0)|SQ(3,1),
            0 },

        { "Third good", 1,
            SQ(0,0)|SQ(0,1)|SQ(1,1)|SQ(2,1)|SQ(3,1),
            SQ(3,0)|SQ(4,0)|SQ(5,0),
            SQ(0,1)|SQ(1,1)|SQ(2,1)|SQ(3,1),
            SQ(1,0) },

        { 0 }
    };

    for (const struct calc_next_steps_data * ptr = data; ptr->active != 0; ++ptr) {
        test_calc_next_steps_data(me, ptr);
    }

    destroy_state(me);
    destroy_geometry(geometry);
    return 0;
}

#endif
