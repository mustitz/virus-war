#include "virus-war.h"

#include <math.h>
#include <string.h>

#define MAX_BLOCKS  (64)
#define BLOCK_SZ    (1024*1024)

#define TERMINAL_MARK  0xFFFF

#define DEFAULT_C   1.4

struct node
{
    int16_t square;
    uint16_t qchildren;
    int32_t score;
    int32_t qgames;
    uint32_t children;
};

struct mcts_ai
{
    void * static_data;
    void * dynamic_data;

    int n;
    int * history;
    size_t qhistory;

    struct node * * game;
    struct step_stat * stats;

    struct multiallocator * multiallocator;

    float C;
};

static int reset_dynamic(
    struct mcts_ai * restrict const me,
    const struct geometry * const geometry)
{
    const int n = geometry->n;
    if (me->n != n) {
        const size_t game_maxlen = 2 * n * n;
        const size_t history_sz = game_maxlen * sizeof(int);
        const size_t game_sz = game_maxlen * sizeof(struct node *);
        const size_t stats_sz = game_maxlen * sizeof(struct step_stat);
        size_t sizes[3] = { history_sz, game_sz, stats_sz };
        void * ptrs[3];
        void * data = multialloc(3, sizes, ptrs, 64);
        if (data == NULL) {
            return ENOMEM;
        }

        if (me->dynamic_data != NULL) {
            free(me->dynamic_data);
        }

        me->history = ptrs[0];
        me->game = ptrs[1];
        me->stats = ptrs[2];
        me->dynamic_data = data;
    }

    me->n = n;
    me->qhistory = 0;
    return 0;
}

static int mcts_ai_reset(
	struct ai * restrict const ai,
	const struct geometry * const geometry)
{
    ai->error = NULL;

    struct mcts_ai * restrict const me = ai->data;
    const int status = reset_dynamic(me, geometry);
    if (status != 0) {
        ai->error = "reset_dynamic fails.";
        return status;
    }

    struct state * restrict const state = &ai->state;
    init_state(state, geometry);
    return 0;
}

static int mcts_ai_do_step(
	struct ai * restrict const ai,
	const int step)
{
    ai->error = NULL;
    struct mcts_ai * restrict const me = ai->data;
    struct state * restrict const state = &ai->state;
    const int status = state_step(state, step);
    if (status != 0) {
        ai->error = "state_step(step) failed.";
        return status;
    }
    me->history[me->qhistory++] = step;
	return 0;
}

static int mcts_ai_do_steps(
	struct ai * restrict const ai,
	const unsigned int qsteps,
	const int steps[])
{
    ai->error = NULL;
    struct mcts_ai * restrict const me = ai->data;
    struct state * restrict const state = &ai->state;

    const size_t saved_qhistory = me->qhistory;
    const struct state saved_state = *state;

    for (int i=0; i<qsteps; ++i) {
        const int status = state_step(state, steps[i]);
        if (status != 0) {
            ai->error = "state_step(step) failed.";
            *state = saved_state;
            me->qhistory = saved_qhistory;
            return status;
        }
    }

	return 0;
}

static int mcts_ai_undo_step(struct ai * restrict const ai)
{
    ai->error = NULL;
    struct state * restrict const state = &ai->state;
    struct mcts_ai * restrict const me = ai->data;
    const size_t qhistory = me->qhistory;

    if (qhistory == 0) {
        ai->error = "History is empty, undo is not possible.";
        return EINVAL;
    }

    const int sq = me->history[qhistory-1];
    const int status = state_unstep(state, sq);
    if (status != 0) {
        ai->error = "state_unstep fails.";
        return status;
    }

    --me->qhistory;
    return 0;
}

static int mcts_ai_undo_steps(struct ai * restrict const ai, const unsigned int qsteps)
{
    ai->error = NULL;
    struct state * restrict const state = &ai->state;
    struct mcts_ai * restrict const me = ai->data;
    const size_t qhistory = me->qhistory;

    if (qhistory < qsteps) {
        ai->error = "History is not enought to unstep given steps.";
        return EINVAL;
    }

    const struct state backup = *state;
    for (int i=1; i<= qsteps; ++i) {
        const int sq = me->history[qhistory-i];
        const int status = state_unstep(state, sq);
        if (status != 0) {
            ai->error = "state_unstep fails.";
            *state = backup;
            return status;
        }
    }

    me->qhistory -= qsteps;
    return 0;
}

static int mcts_ai_go(
	struct ai * restrict const ai,
	struct ai_explanation * restrict const explanation)
{
    const struct state * const state = &ai->state;
    bb_t steps = state_get_steps(state);
    if (steps == 0) {
        ai->error = "No moves";
        errno = EINVAL;
        return -1;
    }

    if (explanation != NULL) {
        explanation->qstats = 0;
        explanation->stats = NULL;
        explanation->time = 0.0;
        explanation->score = 0.5;
    }

    const int qsteps = pop_count(steps);
    if (qsteps == 1) {
        return first_one(steps);
    }

    const int choice = rand() % qsteps;
    return nth_one_index(steps, choice);
}

static const struct ai_param * mcts_ai_get_params(const struct ai * const ai)
{
	static const struct ai_param terminator = { NULL, NULL, NO_TYPE, 0 };
	return &terminator;
}

static int mcts_ai_set_param(
	struct ai * restrict const ai,
	const char * const name,
	const void * const value)
{
	ai->error = "Unknown parameter name.";
	return EINVAL;
}

static void free_mcts_ai(struct ai * restrict const ai)
{
    struct mcts_ai * restrict const me = ai->data;
    destroy_multiallocator(me->multiallocator);
    free(me->dynamic_data);
    free(me->static_data);
}

int init_mcts_ai(
    struct ai * restrict const ai,
    const struct geometry * const geometry)
{
    ai->error = NULL;

    size_t static_data_sz = sizeof(struct mcts_ai);
    void * static_data = malloc(static_data_sz);
    if (static_data == NULL) {
        ai->error = "Cannot allocate static_data";
        return ENOMEM;
    }

    memset(static_data, 0, static_data_sz);
    struct mcts_ai * restrict const me = static_data;

    me->static_data = static_data;
    const int status = reset_dynamic(me, geometry);
    if (status != 0) {
        ai->error = "Cannot alocate dynamic data";
        free(static_data);
        return ENOMEM;
    }

    static const size_t type_sizes[1] = { sizeof(struct node) };
    me->multiallocator = create_multiallocator(
        MAX_BLOCKS,
        BLOCK_SZ,
        1, type_sizes);
    if (me->multiallocator == NULL) {
        ai->error = "create_multiallocator fails";
        free(me->static_data);
        free(me->dynamic_data);
        return errno;
    }

    ai->data = me;
    me->C = DEFAULT_C;

    ai->reset = mcts_ai_reset;
    ai->do_step = mcts_ai_do_step;
    ai->do_steps = mcts_ai_do_steps;
    ai->undo_step = mcts_ai_undo_step;
    ai->undo_steps = mcts_ai_undo_steps;
    ai->go = mcts_ai_go;
    ai->get_params = mcts_ai_get_params;
    ai->set_param = mcts_ai_set_param;
    ai->get_state = ai_get_state;
    ai->free = free_mcts_ai;

    struct state * restrict const state = &ai->state;
    init_state(state, geometry);
    return 0;
}



/* Move selection */

static inline bb_t grow(
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

static inline bb_t next_steps(
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

static inline bb_t select_step(const bb_t steps)
{
    const int qbits = pop_count(steps);
    if (qbits == 1) {
        return steps;
    }

    const int sq = nth_one_index(steps, rand() % qbits);
    return BB_SQUARE(sq);
}

#ifdef MAKE_CHECK
#define DEBUG_LOG_ARG , int * restrict debug_log
#define PUT_DEBUG_LOG(bb) do if (debug_log) { *debug_log++ = (first_one(bb)); } while(0)
#define ROLLOUT_LAST_ARG  , NULL
#else
#define DEBUG_LOG_ARG
#define PUT_DEBUG_LOG(bb)
#define ROLLOUT_LAST_ARG
#endif

int rollout(
    bb_t x, bb_t o, bb_t dead, /* Game data */
    const int n, const bb_t all, const bb_t not_lside, const bb_t not_rside /* Geometry */,
    uint32_t * restrict const qthink DEBUG_LOG_ARG)
{
    static void *labels[10] =
        { &&step0, &&step1, &&step2, &&step3, &&step4,
          &&step5, &&step6, &&step7, &&step8, &&step9 };
    const int all_qsteps = pop_count(x|o) + pop_count(dead);
    const int index = all_qsteps < 10 ? all_qsteps : ((all_qsteps-4) %6) + 4;
    goto *labels[index];

    step4: {
        ++*qthink;
        bb_t steps = next_steps(o, x, dead, n, all, not_lside, not_rside);
        if (steps == 0) {
            return +1;
        }
        const bb_t bb = select_step(steps);
        *(bb & x ? &dead : &o) |= bb;
        PUT_DEBUG_LOG(bb);
    }

    step5: {
        ++*qthink;
        bb_t steps = next_steps(o, x, dead, n, all, not_lside, not_rside);
        if (steps == 0) {
            return +1;
        }
        const bb_t bb = select_step(steps);
        *(bb & x ? &dead : &o) |= bb;
        PUT_DEBUG_LOG(bb);
    }

    step6: {
        ++*qthink;
        bb_t steps = next_steps(x, o, dead, n, all, not_lside, not_rside);
        if (steps == 0) {
            return -1;
        }
        const bb_t bb = select_step(steps);
        *(bb & o ? &dead : &x) |= bb;
        PUT_DEBUG_LOG(bb);
    }

    step7: {
        ++*qthink;
        bb_t steps = next_steps(x, o, dead, n, all, not_lside, not_rside);
        if (steps == 0) {
            return -1;
        }
        const bb_t bb = select_step(steps);
        *(bb & o ? &dead : &x) |= bb;
        PUT_DEBUG_LOG(bb);
    }

    step8: {
        ++*qthink;
        bb_t steps = next_steps(x, o, dead, n, all, not_lside, not_rside);
        if (steps == 0) {
            return -1;
        }
        const bb_t bb = select_step(steps);
        *(bb & o ? &dead : &x) |= bb;
        PUT_DEBUG_LOG(bb);
    }

    step9: {
        ++*qthink;
        bb_t steps = next_steps(o, x, dead, n, all, not_lside, not_rside);
        if (steps == 0) {
            return +1;
        }
        const bb_t bb = select_step(steps);
        *(bb & x ? &dead : &o) |= bb;
        PUT_DEBUG_LOG(bb);
    }

    goto step4;

    step0: {
        ++*qthink;
        x |= BB_SQUARE(0);
        PUT_DEBUG_LOG(BB_SQUARE(0));
    }

    step1: {
        ++*qthink;
        bb_t steps = next_steps(x, o, dead, n, all, not_lside, not_rside);
        if (steps == 0) {
            return -1;
        }
        const bb_t bb = select_step(steps);
        *(bb & o ? &dead : &x) |= bb;
        PUT_DEBUG_LOG(bb);
    }

    step2: {
        ++*qthink;
        bb_t steps = next_steps(x, o, dead, n, all, not_lside, not_rside);
        if (steps == 0) {
            return -1;
        }
        const bb_t bb = select_step(steps);
        *(bb & o ? &dead : &x) |= bb;
        PUT_DEBUG_LOG(bb);
    }

    step3: {
        ++*qthink;
        o |= BB_SQUARE(n*n-1);
        PUT_DEBUG_LOG(BB_SQUARE(n*n-1));
    }

    goto step4;
}

static inline int is_leaf(const struct node * const node)
{
    return node->qchildren == 0;
}

static inline int is_terminal(const struct node * const node)
{
    return node->qchildren == TERMINAL_MARK;
}

static inline struct node * get_node(
    struct mcts_ai * restrict const me,
    size_t inode)
{
    return multiallocator_get(me->multiallocator, 0, inode);
}

static void update_game_history(
    const int result,
    struct node * * game, const size_t game_len,
    int active, int all_qsteps)
{
    struct node * restrict const root = game[0];
    ++root->qgames;
    root->score += result;

    for (int i=1; i<game_len; ++i) {
        struct node * restrict const node = game[i];
        ++node->qgames;
        if (active == ACTIVE_X) {
            node->score += result;
        } else {
            node->score -= result;
        }

        ++all_qsteps;
        if ((all_qsteps % 3) == 0) {
            active ^= 3;
        }
    }
}

static int ubc_select_step(
    struct mcts_ai * restrict const me,
    const struct node * const node)
{
    const int qchildren = node->qchildren;
    if (qchildren == 1) {
        return 0;
    }

    int qbest = 0;
    int best_indexes[qchildren];
    float best_weight = -1.0e+10f;
    const float total = node->qgames;
    const float log_total = log(total);
    const struct node * child = get_node(me, node->children);
    for (int i=0; i<qchildren; ++i) {
        const float score = child->qgames ? child->score : 2;
        const float qgames = child->qgames ? child->qgames : 1;
        const float ev = score / qgames;
        const float investigation = sqrt(log_total/qgames);
        const float weight = ev + me->C * investigation;

        if (weight >= best_weight) {
            if (weight != best_weight) {
                qbest = 0;
                best_weight = weight;
            }
            best_indexes[qbest++] = i;
        }

        ++child;
    }

    const int index = qbest == 1 ? 0 : rand() % qbest;
    const int choice = best_indexes[index];
    return choice;
}

int simulate(
    struct mcts_ai * restrict const me,
    struct node * restrict node,
    uint32_t * restrict const qthink,
    bb_t x, bb_t o, bb_t dead, /* Game data */
    const int n, const bb_t all, const bb_t not_lside, const bb_t not_rside /* Geometry */)
{
    struct node * * game = me->game;
    size_t game_len = 0;

    const int start_qsteps = pop_count(x|o) + pop_count(dead);
    const int start_mod = (start_qsteps/3) % 2;
    const int start_active = start_mod == 0 ? ACTIVE_X : ACTIVE_O;

    bb_t * my = start_active == ACTIVE_X ? &x : &o;
    bb_t * opp = start_active == ACTIVE_X ? &o : &x;

    int all_qsteps = start_qsteps;
    int active = start_active;
    for (;;) {
        game[game_len++] = node;
        ++*qthink;

        if (is_leaf(node)) {
            break;
        }

        if (is_terminal(node)) {
            const int result = active == ACTIVE_X ? -1 : +1;
            update_game_history(result, game, game_len, start_active, start_qsteps);
            return 0;
        }

        const int index = ubc_select_step(me, node);
        node = get_node(me, node->children + index);
        const int sq = node->square;
        const bb_t bb = BB_SQUARE(sq);
        *(bb & *opp ? &dead : my) |= bb;
        ++all_qsteps;

        if ((all_qsteps % 3) == 0) {
            bb_t * const tmp = my;
            my = opp;
            opp = tmp;
            active ^= 3;
        }
    }

    bb_t steps;
    if (all_qsteps == 0) {
        steps = BB_SQUARE(0);
    } else if (all_qsteps == 3) {
        steps = BB_SQUARE(n*n-1);
    } else {
        steps = next_steps(*my, *opp, dead, n, all, not_lside, not_rside);
    }

    const int qsteps = pop_count(steps);
    if (qsteps == 0) {
        node->qchildren = TERMINAL_MARK;
        const int result = active == ACTIVE_X ? -1 : +1;
        update_game_history(result, game, game_len, start_active, start_qsteps);
        return 0;
    }

    const size_t inode = multiallocator_allocn(me->multiallocator, 0, qsteps);
    if (inode == BAD_ALLOC_INDEX) {
        return ENOMEM;
    }

    struct node * restrict child = get_node(me, inode);
    for (int i=0; i<qsteps; ++i) {
        const int sq = first_one(steps);
        steps ^= BB_SQUARE(sq);

        child->square = sq;
        child->qchildren = 0;
        child->score = 0;
        child->qgames = 0;
        child->children = 0;
        ++child;
    }

    node->qchildren = qsteps;
    node->children = inode;

    const int result = rollout(x, o, dead, n, all, not_lside, not_rside, qthink ROLLOUT_LAST_ARG);
    update_game_history(result, game, game_len, start_active, start_qsteps);
    return 0;
}



#ifdef MAKE_CHECK

#include "insider.h"

#include <stdio.h>

void check_rollout(
    const int auto_steps,
    struct geometry * restrict const geometry)
{
    struct state * restrict const me = create_state(geometry);
    if (me == NULL) {
        test_fail("create_state(geometry) failed, errno = %d.", errno);
    }

    for (int i=0; i<auto_steps; ++i) {
        bb_t steps = state_get_steps(me);
        if (steps == 0) {
            test_fail("cannot perform auto steps, state_get_steps(me) returns 0.");
        }

        const int qsteps = pop_count(steps);
        const int sq = nth_one_index(steps, rand() % qsteps);
        const int status = state_step(me, sq);
        if (status != 0) {
            test_fail("state_step(me, %d) fails with code %d, %s.", sq, status, strerror(status));
        }
    }

    const bb_t x = me->x;
    const bb_t o = me->o;
    const bb_t dead = me->dead;
    const int n = geometry->n;
    const bb_t all = geometry->all;
    const bb_t not_lside = all ^ geometry->lside;
    const bb_t not_rside = all ^ geometry->rside;
    uint32_t qthink = 0;
    int debug_log[2*n*n];
    const int result = rollout(x, o, dead, n, all, not_lside, not_rside, &qthink, debug_log);

    if (result != +1 &&result != -1) {
        test_fail("rollout returns strange result %d", result);
    }

    if (qthink-- == 0) {
        test_fail("Unexpected zero qthink after rollout call.");
    }


    /* Tricky!!! Round qthink to avoid last nonvalid moves at the end. */
    /* rollout may play one or two steps before realizing that there is no moves */
    qthink = (3*((qthink+auto_steps)/3)) - auto_steps;

    for (int i=0; i<qthink; ++i) {
        if (state_status(me) != 0) {
            test_fail("Unexpected state status %d during applying rollout history.", state_status(me));
        }
        const int sq = debug_log[i];
        const int status = state_step(me, sq);
        if (status != 0) {
            test_fail("state_step(%d) failed with code %d, %s.", sq, status, strerror(status));
        }
    }

    if (state_status(me) == 0) {
        test_fail("Unexpected state status %d after applying rollout history.", state_status(me));
    }

    destroy_state(me);
}

int test_rollout(void)
{
    struct geometry * restrict const geometry4 = create_std_geometry(4);
    if (geometry4 == NULL) {
        test_fail("create_std_geometry(4) failed, errno = %d.", errno);
    }

    struct geometry * restrict const geometry10 = create_std_geometry(10);
    if (geometry10 == NULL) {
        test_fail("create_std_geometry(10) failed, errno = %d.", errno);
    }

    struct geometry * restrict const geometry11 = create_std_geometry(11);
    if (geometry11 == NULL) {
        test_fail("create_std_geometry(11) failed, errno = %d.", errno);
    }

    check_rollout(0, geometry4);
    check_rollout(10, geometry4);
    check_rollout(0, geometry10);
    check_rollout(1, geometry10);
    check_rollout(2, geometry11);
    check_rollout(3, geometry11);
    check_rollout(4, geometry10);
    check_rollout(5, geometry10);
    check_rollout(6, geometry11);
    check_rollout(7, geometry11);
    check_rollout(8, geometry10);
    check_rollout(9, geometry10);
    check_rollout(10, geometry11);
    check_rollout(11, geometry11);
    check_rollout(12, geometry10);
    check_rollout(13, geometry10);
    check_rollout(14, geometry11);
    check_rollout(15, geometry11);

    destroy_geometry(geometry4);
    destroy_geometry(geometry10);
    destroy_geometry(geometry11);
    return 0;
}

int test_mcts_init_free(void)
{
    struct geometry * restrict const geometry = create_std_geometry(11);
    if (geometry == NULL) {
        test_fail("create_std_geometry(11) failed, errno = %d.", errno);
    }

    struct ai storage;
    const int status = init_mcts_ai(&storage, geometry);
    if (status != 0) {
        test_fail("init_mcts_ai fails with code %d, %s.", status, strerror(status));
    }

    struct ai * restrict const ai = &storage;
    ai->free(ai);

    destroy_geometry(geometry);
    return 0;
}

void rnd_steps(
    struct ai * restrict const ai,
    const struct geometry * const geometry,
    const int rnd_qsteps)
{
    ai->reset(ai, geometry);
    for (int i=0; i<rnd_qsteps; ++i) {
        const bb_t steps = state_get_steps(&ai->state);
        const int qsteps = pop_count(steps);
        if (qsteps == 0) {
            test_fail("Unexpected zero qsteps.");
        }
        const int choice = rand() % qsteps;
        const int sq = nth_one_index(steps, choice);
        const int status = state_step(&ai->state, sq);
        if (status != 0) {
            test_fail("state_step failed on %d-th step.", i);
        }
    }
}

void check_simulate(
    const struct geometry * const geometry,
    struct mcts_ai * restrict const me,
    const struct state * const state,
    const int qruns)
{
    multiallocator_reset(me->multiallocator);

    const size_t inode = multiallocator_alloc(me->multiallocator, 0);
    if (inode == BAD_ALLOC_INDEX) {
        test_fail("multiallocator->alloc(0) failed.");
    }

    struct node * restrict const node = get_node(me, inode);
    node->square = -1;
    node->qchildren = 0;
    node->score = 0;
    node->qgames = 0;
    node->children = 0;

    uint32_t qthink = 0;
    const bb_t x = state->x;
    const bb_t o = state->o;
    const bb_t dead = state->dead;

    const int n = geometry->n;
    const bb_t all = geometry->all;
    const bb_t not_lside = all ^ geometry->lside;
    const bb_t not_rside = all ^ geometry->rside;

    for (int i=0; i<qruns; ++i) {
        uint32_t saved_qthink = qthink;
        const int status = simulate(me, node, &qthink, x, o, dead, n, all, not_lside, not_rside);
        if (status != 0) {
            test_fail("Unexpected status %d returned from %d-th simulate(...), %s.", status, i, strerror(status));
        }
        if (qthink == saved_qthink) {
            test_fail("qthink might be increased after %d-th simulation.", i);
        }
    }
}

int test_simulate(void)
{
    struct geometry * restrict const geometry = create_std_geometry(4);
    if (geometry == NULL) {
        test_fail("create_std_geometry(4) failed, errno = %d.", errno);
    }

    struct ai storage;
    const int status = init_mcts_ai(&storage, geometry);
    if (status != 0) {
        test_fail("init_mcts_ai fails with code %d, %s.", status, strerror(status));
    }

    struct ai * restrict const ai = &storage;
    struct mcts_ai * restrict const me = ai->data;

    const struct state * const state = ai->get_state(ai);
    for (int i=0; i<=10; ++i) {
        rnd_steps(ai, geometry, i);
        check_simulate(geometry, me, state, 100);
    }

    ai->free(ai);

    destroy_geometry(geometry);
    return 0;
}

#endif
