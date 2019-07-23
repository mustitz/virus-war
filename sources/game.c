#include "virus-war.h"

const size_t param_sizes[QPARAM_TYPES] = {
    [U32] = sizeof(uint32_t),
    [I32] = sizeof(int32_t),
    [F32] = sizeof(float),
};

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



void init_state(
    struct state * restrict const me,
    const struct geometry * const geometry)
{
    me->geometry = geometry;
    me->active = ACTIVE_X;
    me->x = 0;
    me->o = 0;
    me->dead = 0;
    me->next = geometry->x_first_step;
}

struct state * create_state(const struct geometry * const geometry)
{
    size_t sz = sizeof(struct state);
    struct state * restrict const me = malloc(sz);
    if (me == NULL) {
        return NULL;
    }

    init_state(me, geometry);
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

static bb_t calc_next_steps(
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

int state_step(
    struct state * restrict const me,
    const int step)
{
    const bb_t bb = BB_SQUARE(step);
    const int bad = (bb & me->next) == 0;
    if (bad) {
        return errno = EINVAL;
    }

    bb_t * restrict const my = me->active == ACTIVE_X ? &me->x : &me->o;
    bb_t * restrict const opp = me->active != ACTIVE_X ? &me->x : &me->o;

    if (bb & *opp) {
        me->dead |= bb;
    } else {
        *my |= bb;
    }

    const int qsteps = pop_count(*my|*opp) + pop_count(me->dead);
    const int mod = qsteps % 3;
    if (mod == 0) {
        me->active ^= 3;
    }

    me->next = calc_next_steps(me);
    return 0;
}



#ifdef MAKE_CHECK

#include "insider.h"

#include <stdio.h>

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

static const int file_chars[256] = {
    [0 ... 'a'-1] = -1,
    ['a'] = 0, ['b'] = 1, ['c'] = 2, ['d'] = 3, ['e'] = 4,
    ['f'] = 5, ['g'] = 6, ['h'] = 7, ['i'] = 8, ['j'] = -1, ['k'] = 9,
    ['l' ... 255] = -1
};

static const int rank_chars[256] = {
    [0 ... '0'] = -1,
    ['1'] = 0, ['2'] = 1, ['3'] = 2, ['4'] = 3, ['5'] = 4,
    ['6'] = 5, ['7'] = 6, ['8'] = 7, ['9'] = 8,
    ['9' + 1 ... 'T'-1] = -1,
    ['T'] = 9,
    ['T'+1 ... 255] = -1
};

static int parse_sq(const char * const s)
{
    if (s == NULL) {
        test_fail("Cannot parse NULL.");
    }

    const int file_ch = s[0];
    const int file = file_chars[file_ch];
    if (file < 0) {
        test_fail("Invalid file char with code %d (“%c”)", file_ch, file_ch);
    }

    const int rank_ch = s[1];
    const int rank = rank_chars[rank_ch];
    if (rank < 0) {
        test_fail("Invalid rank char with code %d (“%c”)", rank_ch, rank_ch);
    }

    return 10 * rank + file;
}

#undef SQ
#define SQ(sq)  BB_SQUARE(parse_sq(#sq))

struct move
{
    const char * sq;
    bb_t expansion;
    bb_t folding;
};

void print_mismatch(const bb_t steps, const bb_t expected)
{
    const char * const files = "abcdefghik";
    const char * const ranks = "123456789T";

    printf("STEPS:   ");
    for (int i=0; i<100; ++i) {
        if (BB_SQUARE(i) & steps) {
            printf(" %c%c", files[i%10], ranks[i/10]);
        }
    }

    printf("\nEXPECTED:");
    for (int i=0; i<100; ++i) {
        if (BB_SQUARE(i) & expected) {
            printf(" %c%c", files[i%10], ranks[i/10]);
        }
    }

    const bb_t diff = steps ^ expected;
    printf("\nDIFF:    ");
    for (int i=0; i<100; ++i) {
        if (BB_SQUARE(i) & diff) {
            printf(" %c%c", files[i%10], ranks[i/10]);
        }
    }

    printf("\n");
}

int test_game(void)
{
    struct geometry * restrict const geometry = create_std_geometry(10);
    if (geometry == NULL) {
        test_fail("create_std_geometry(%d) failed, errno = %d.", N, errno);
    }

    struct state * restrict const me = create_state(geometry);
    if (me == NULL) {
        test_fail("create_state(geometry) failed, errno = %d.", errno);
    }

    struct move game[] = {
        { "a1", SQ(a2)|SQ(b2)|SQ(b1), 0 },
        { "b2", SQ(a3)|SQ(b3)|SQ(c3)|SQ(c2)|SQ(c1), 0 },
        { "c3", SQ(b4)|SQ(c4)|SQ(d4)|SQ(d3)|SQ(d2), 0 },

        { "kT", SQ(k9)|SQ(i9)|SQ(iT), 0 },
        { "i9", SQ(hT)|SQ(h9)|SQ(h8)|SQ(i8)|SQ(k8), 0 },
        { "h8", SQ(i7)|SQ(h7)|SQ(g7)|SQ(g8)|SQ(g9), 0 },

        { "d4", SQ(c5)|SQ(d5)|SQ(e5)|SQ(e4)|SQ(e3), 0 },
        { "d5", SQ(c6)|SQ(d6)|SQ(e6), 0 },
        { "e4", SQ(f3)|SQ(f4)|SQ(f5), 0 },

        { "g7", SQ(h6)|SQ(g6)|SQ(f6)|SQ(f7)|SQ(f8), 0 },
        { "f8", SQ(e7)|SQ(e8)|SQ(e9)|SQ(f9), 0 },
        { "h6", SQ(g5)|SQ(h5)|SQ(i5)|SQ(i6), 0 },

        { "f3", SQ(e2)|SQ(f2)|SQ(g2)|SQ(g3)|SQ(g4)|SQ(f4), 0 },
        { "g2", SQ(f1)|SQ(g1)|SQ(h1)|SQ(h2)|SQ(h3), 0 },
        { "c6", SQ(d6)|SQ(d7)|SQ(c7)|SQ(b7)|SQ(b6)|SQ(b5), 0 },

        { "e9", SQ(d8)|SQ(d9)|SQ(dT)|SQ(eT)|SQ(fT), 0 },
        { "dT", SQ(c9)|SQ(cT), 0 },
        { "cT", SQ(b9)|SQ(bT), 0 },

        { "h3", SQ(h4)|SQ(i4)|SQ(i3)|SQ(i2), 0 },
        { "i3", SQ(k2)|SQ(k3)| SQ(k4), 0 },
        { "b6", SQ(a5)|SQ(a6)|SQ(a7), 0 },

        { "i6", SQ(k5)|SQ(k6)|SQ(k7), 0 },
        { "k6", 0, 0 },
        { "bT", SQ(b9)|SQ(a9)|SQ(aT), 0 },

        { "k3", 0, 0 },
        { "g3", 0, 0 },
        { "a7", SQ(a6)|SQ(a8)|SQ(b8), 0 },

        { "aT", 0, 0 },
        { "a9", SQ(a8)|SQ(b8), 0 },
        { "d9", SQ(c8), 0 },

        { "a8", SQ(a9)|SQ(b9), 0 },
        { "a9", SQ(aT)|SQ(bT), SQ(a8)|SQ(b8) },
        { "k4", SQ(i5)|SQ(k5), 0 },

        { "b9", SQ(a8)|SQ(b8), SQ(aT)|SQ(bT) },
        { "a8", SQ(a7)|SQ(b7), SQ(b9) },
        { "a7", SQ(a6)|SQ(b6), SQ(b8) },

        { "k5", SQ(i6)|SQ(k6), 0 },
        { "k6", SQ(i7)|SQ(k7), 0 },
        { "i6", SQ(h5)|SQ(h6)|SQ(h7), SQ(k5)|SQ(k7) },

        { "b6", SQ(a5)|SQ(b5)|SQ(c5)|SQ(c6)|SQ(c7), SQ(a5)|SQ(a6) },
        { "c6", SQ(d5)|SQ(d6)|SQ(d7), SQ(b5)|SQ(b7)|SQ(c7)|SQ(d7) },
        { "d5", SQ(c4)|SQ(d4)|SQ(e4)|SQ(e5)|SQ(e6), SQ(d6)|SQ(e6) },

        { "a3", SQ(a4), 0 },
        { "c5", SQ(b5)|SQ(d6), 0 },
        { "h6", SQ(g5)|SQ(g6)|SQ(g7), SQ(g5)|SQ(h5)|SQ(i5) },

        { "e4", SQ(d3)|SQ(e3)|SQ(f3)|SQ(f4)|SQ(f5), SQ(f5) },
        { "f3", SQ(e2)|SQ(f2)|SQ(g2)|SQ(g3)|SQ(g4), SQ(e2) },
        { "g3", SQ(h2)|SQ(h3)|SQ(h4), SQ(f4) },

        { "g7", SQ(f6)|SQ(f7)|SQ(f8)|SQ(g8)|SQ(h8), SQ(f6)|SQ(g6) },
        { "h8", SQ(i8)|SQ(i9)|SQ(h9)|SQ(g9), SQ(h7)|SQ(i7) },
        { "g9", SQ(f9)|SQ(fT)|SQ(gT)|SQ(hT), 0 },

        { "g9", SQ(gT), SQ(f9)|SQ(fT)|SQ(gT)|SQ(hT) },
        { "h3", SQ(i2)|SQ(i3)|SQ(i4), SQ(g4) },
        { "i3", SQ(k2)|SQ(k3)|SQ(k4), SQ(h4) },

        { "i9", SQ(hT)|SQ(iT)|SQ(kT)|SQ(k9)|SQ(k8), SQ(i8)|SQ(k8) },
        { "f6", SQ(f5)|SQ(e6)|SQ(e7), 0 },
        { "f8", SQ(e8)|SQ(e9)|SQ(f9), SQ(e7)|SQ(f7)|SQ(g8)|SQ(gT)|SQ(h9)|SQ(hT) },

        { "k4", SQ(i5)|SQ(k5), 0 },
        { "k5", 0, 0 },
        { "d4", SQ(c3), SQ(e3) },

        { "h5", SQ(i5)|SQ(i4)|SQ(h4)|SQ(g4), 0 },
        { "k2", SQ(i1)|SQ(k1), 0 },
        { "k1", 0, 0 },

        { "h4", SQ(g5)|SQ(h5), 0 },
        { "h5", SQ(g6), SQ(g4)|SQ(h4) },
        { "k3", 0, SQ(i4) },

        { "h1", 0, 0 },
        { "f1", SQ(e1)|SQ(e2), 0 },
        { "hT", SQ(gT), 0 },

        { "g2", SQ(f1)|SQ(g1)|SQ(h1), 0 },
        { "h1", SQ(i1), SQ(h2) },
        { "k2", SQ(k1), 0 },

        { "i2", SQ(h2), 0 },
        { "f2", SQ(e3), 0 },
        { "c1", SQ(d1), 0 },

        { "i2", 0, SQ(h2) },
        { "k1", 0, SQ(i1) },
        { "f2", SQ(e1), SQ(e3) },

        { "g1", SQ(h2), 0 },
        { "h2", SQ(i1), 0 },
        { "i1", 0, 0 },

        { "f1", 0, SQ(e1)|SQ(e2) },
        { "c3", SQ(b4)|SQ(b3)|SQ(b2)|SQ(c2)|SQ(d2), SQ(d3) },
        { "b2", SQ(a3)|SQ(a2)|SQ(a1)|SQ(b1)|SQ(c1), 0 },

        { "d1", SQ(e1)|SQ(e2), 0 },
        { "e1", 0, 0 },
        { "a4", SQ(a5), 0 },

        { "c1", SQ(d1), 0 },
        { "d1", 0, SQ(c2) },
        { "e1", 0, SQ(d2)|SQ(e2) },

        { "c4", SQ(d3), 0 },
        { "d3", SQ(c2)|SQ(d2)|SQ(e2)|SQ(e3), 0 },
        { "e2", 0, 0 },

        { "d3", 0, SQ(c2) },
        { "e2", 0, SQ(d2)|SQ(e3) },
        { "a1", 0, SQ(b1) },

        { "a2", SQ(b1), 0 },
        { "b1", SQ(c2), 0 },
        { "c2", SQ(d2), 0 },

        { "k9", SQ(i8)|SQ(k8), 0 },
        { "k8", SQ(i7)|SQ(k7), 0 },
        { "k7", 0, 0 },

        { "k9", 0, 0 },
        { "kT", 0, SQ(iT) },
        { "e9", SQ(dT)|SQ(d9)|SQ(d8)|SQ(eT)|SQ(fT), SQ(f9)|SQ(fT) },

        { "eT", SQ(f9)|SQ(fT), 0 },
        { "fT", SQ(gT)|SQ(g8)|SQ(h9)|SQ(hT), 0 },
        { "hT", SQ(iT), SQ(gT) },

        { "fT", SQ(gT), SQ(g8)|SQ(gT)|SQ(h9)|SQ(iT) },
        { "iT", 0, 0 },
        { "d9", SQ(c8)|SQ(c9)|SQ(cT), SQ(d8)|SQ(e8) },

        { "f9", SQ(e8)|SQ(g8)|SQ(gT)|SQ(h9)|SQ(iT), 0 },
        { "iT", 0, 0 },
        { "gT", 0, 0 },

        { "f9", 0, SQ(e8) },
        { "d2", SQ(e3), 0 },
        { "e8", SQ(d7), 0 },

        { "d2", 0, SQ(e3) },
        { "e3", 0, 0 },
        { "i7", SQ(h7), 0 },

        { "i7", 0, SQ(h7) },
        { "c8", SQ(c7)|SQ(b7)|SQ(b8)|SQ(b9), 0 },
        { "b9", SQ(aT)|SQ(bT), SQ(c8) },

        { "b7", SQ(c8), 0 },
        { "c8", SQ(d8), SQ(b8)|SQ(c7)|SQ(b7)|SQ(aT)|SQ(bT) },
        { "g8", SQ(f7)|SQ(h7), 0 },

        { "g8", 0, SQ(f7)|SQ(h7) },
        { "i5", SQ(h4)|SQ(i4), 0 },
        { "h4", SQ(g4), SQ(g5)|SQ(g6) },

        { "h9", 0, 0 },
        { "i8", SQ(h7), 0 },
        { "i5", SQ(g5)|SQ(g6), SQ(g4)|SQ(i4) },

        { "a5", SQ(a6), 0 },
        { "a6", SQ(b7), 0 },
        { "b7", SQ(b8)|SQ(c7), SQ(d8) },

        { "e5", SQ(f6), 0 },
        { "f6", SQ(e7)|SQ(f7), SQ(f5)|SQ(e5)|SQ(e6) },
        { "i4", 0, 0 },

        { "g5", SQ(g4)|SQ(f4)|SQ(f5)|SQ(i4), 0 },
        { "f4", SQ(e3)|SQ(e5), 0 },
        { "e3", 0, 0 },

        { "d7", SQ(d8)|SQ(e8), 0 },
        { "e8", 0, SQ(d7) },
        { "g5", 0, SQ(h7)|SQ(g6)|SQ(f7)|SQ(e7)|SQ(d8)|SQ(c9)|SQ(i4)|SQ(k7)|SQ(k8)|SQ(i8)|SQ(h9)|SQ(gT)|SQ(eT)|SQ(dT)|SQ(cT) },

        { "g4", SQ(i4), 0 },
        { "i4", 0, 0 },
        { "e5", SQ(e6), 0 },

        { "a6", 0, SQ(c7)|SQ(b8) },
        { "c5", 0, 0 },
        { "f4", 0, SQ(d6)|SQ(e6) },

        { "f5", SQ(d6)|SQ(e6)|SQ(g6), 0 },
        { "e6", SQ(d7)|SQ(e7)|SQ(f7), 0 },
        { "d7", SQ(c7)|SQ(d8), SQ(d8)|SQ(a2)|SQ(a3)|SQ(a5)|SQ(b3)|SQ(b4)|SQ(b5)|SQ(c4)|SQ(g1)|SQ(h2)|SQ(i1)|SQ(g4)|SQ(f5)|SQ(e6)|SQ(g6)|SQ(f7)|SQ(e7)|SQ(d6)|SQ(c7)|SQ(b8)|SQ(b1)|SQ(c2) },

        { "h7", SQ(g6), 0 },
        { "g6", SQ(b1)|SQ(g1)|SQ(i1)|SQ(a2)|SQ(c2)|SQ(h2)|SQ(a3)|SQ(b3)|SQ(b4)|SQ(c4)|SQ(g4)|SQ(a5)|SQ(b5)|SQ(f5)|SQ(d6)|SQ(e6)|SQ(c7)|SQ(e7)|SQ(f7)|SQ(b8), 0 },
        { "f5", 0, SQ(g6) },

        { "f7", SQ(cT)|SQ(dT)|SQ(eT)|SQ(gT)|SQ(c9)|SQ(h9)|SQ(d8)|SQ(i8)|SQ(k8)|SQ(h7)|SQ(k7)|SQ(g6), 0 },
        { "g6", 0, SQ(b1)|SQ(g1)|SQ(i1)|SQ(a2)|SQ(c2)|SQ(h2)|SQ(a3)|SQ(b3)|SQ(b4)|SQ(c4)|SQ(g4)|SQ(a5)|SQ(b5)|SQ(d6)|SQ(e6)|SQ(c7)|SQ(e7)|SQ(f7)|SQ(b8) },
        { "cT", SQ(aT)|SQ(bT)|SQ(b8), 0 },

        { "c9", SQ(b8)|SQ(c7)|SQ(d8), 0 },
        { "c7", SQ(b1)|SQ(g1)|SQ(i1)|SQ(a2)|SQ(c2)|SQ(h2)|SQ(a3)|SQ(b3)|SQ(b4)|SQ(c4)|SQ(g4)|SQ(a5)|SQ(b5)|SQ(d6)|SQ(e6)|SQ(e7)|SQ(f7)|SQ(b8), 0 },
        { "f7", 0, SQ(h7)|SQ(k7)|SQ(b8)|SQ(i8)|SQ(k8)|SQ(c9)|SQ(h9)|SQ(aT)|SQ(bT)|SQ(dT)|SQ(eT)|SQ(gT) },

        { "c7", SQ(b8), SQ(b1)|SQ(g1)|SQ(i1)|SQ(a2)|SQ(c2)|SQ(h2)|SQ(a3)|SQ(b3)|SQ(b4)|SQ(c4)|SQ(g4)|SQ(a5)|SQ(b5)|SQ(d6)|SQ(e6)|SQ(e7) },
        { "d8", SQ(aT)|SQ(bT)|SQ(c9)|SQ(b8)|SQ(dT)|SQ(eT)|SQ(gT)|SQ(h9)|SQ(h7)|SQ(i8)|SQ(k7)|SQ(k8), 0 },
        { "c9", 0, SQ(b8)|SQ(d8) },

        { NULL, 0, 0}
    };

    int move_num = 0;
    bb_t expected_x = SQ(a1);
    bb_t expected_o = SQ(kT);

    bb_t steps = state_get_steps(me);
    if (me->active != 1) {
        test_fail("Error at the beginning: invalid active %d, expected 1.", me->active);
    }
    if (steps != expected_x) {
        test_fail("Error at the beginning: invalid possible steps.");
    }

    for (const struct move * ptr = game; ptr->sq != NULL; ++ptr) {
        const int mod = (move_num/3) % 2;
        const int active = mod == 0 ? ACTIVE_X : ACTIVE_O;
        const int printable_move_num = (move_num / 6) + 1;
        const int printable_step_num = (move_num % 3) + 1;

        ++move_num;
        const int next_mod = (move_num / 3) % 2;
        const int next_active = next_mod == 0 ? ACTIVE_X : ACTIVE_O;

        const int sq = parse_sq(ptr->sq);
        const int status = state_step(me, sq);
        if (status != 0) {
            test_fail("Unexpected status for %s on move %d, step %d.",
                active == ACTIVE_X ? "X" : "O",
                printable_move_num, printable_step_num);
        }

        if (me->active != next_active) {
            test_fail("Unexpected active %s, expected %s on move  %d, step %d.",
                me->active == ACTIVE_X ? "X" : "O",
                next_active == ACTIVE_X ? "X" : "O",
                printable_move_num, printable_step_num);
        }

        if (active == ACTIVE_X) {
            expected_x ^= BB_SQUARE(parse_sq(ptr->sq));
            expected_x |= ptr->expansion;
            expected_o ^= ptr->folding;
        } else {
            expected_o ^= BB_SQUARE(parse_sq(ptr->sq));
            expected_o |= ptr->expansion;
            expected_x ^= ptr->folding;
        }

        const bb_t steps = state_get_steps(me);
        const bb_t expected = next_active == ACTIVE_X ? expected_x : expected_o;
        if (steps != expected) {
            print_mismatch(steps, expected);
            test_fail("Unexpected steps for %s on move %d, step %d.",
                active == ACTIVE_X ? "X" : "O",
                printable_move_num, printable_step_num);
        }
    }

    const bb_t zero_steps = state_get_steps(me);
    if (zero_steps != 0) {
        print_mismatch(zero_steps, 0);
        test_fail("Steps after end of game might be zero (no moves)");
    }

    destroy_state(me);
    destroy_geometry(geometry);
    return 0;
}

#endif
