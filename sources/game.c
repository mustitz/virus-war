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
    return me;
}

void destroy_state(struct state * restrict const me)
{
    free(me);
}



#ifdef MAKE_CHECK

#include "insider.h"

#define  N  9

enum square_placement
{
    UNDEFINED,
    LEFT_SIDE,
    RIGHT_SIDE
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

    switch (square_placement) {

        case LEFT_SIDE:
            if (!lside) {
                test_fail("Board size %d, square %d/%d expected to be on left side.", N, x, y);
            }
            break;

        case RIGHT_SIDE:
            if (!rside) {
                test_fail("Board size %d, square %d/%d expected to be on right side.", N, x, y);
            }
            break;

        case UNDEFINED:
            if (lside || rside) {
                test_fail("Board size %d, square %d/%d expected to be undefined.", N, x, y);
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

    destroy_geometry(me);
    return 0;
}

#endif
