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
