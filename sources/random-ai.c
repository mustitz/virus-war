#include "virus-war.h"

#include <string.h>

struct random_ai
{
    void * static_data;
    void * dynamic_data;

    int n;
    int * history;
    size_t qhistory;
};

static int reset_dynamic(
    struct random_ai * restrict const me,
    const struct geometry * const geometry)
{
    const int n = geometry->n;
    if (me->n != n) {
        const size_t history_maxlen = 2 * n * n;
        size_t history_sz = history_maxlen * sizeof(int);
        void * history = realloc(me->dynamic_data, history_sz);
        if (history == NULL) {
            return ENOMEM;
        }

        me->history = history;
        me->dynamic_data = history;
    }

    me->n = n;
    me->qhistory = 0;
    return 0;
}

static int random_ai_reset(
	struct ai * restrict const ai,
	const struct geometry * const geometry)
{
    ai->error = NULL;

    struct random_ai * restrict const me = ai->data;
    const int status = reset_dynamic(me, geometry);
    if (status != 0) {
        ai->error = "reset_dynamic fails.";
        return status;
    }

    struct state * restrict const state = &ai->state;
    init_state(state, geometry);
    return 0;
}

static int random_ai_do_step(
	struct ai * restrict const ai,
	const int step)
{
    ai->error = NULL;
    struct random_ai * restrict const me = ai->data;
    struct state * restrict const state = &ai->state;
    const int status = state_step(state, step);
    if (status != 0) {
        ai->error = "state_step(step) failed.";
        return status;
    }
    me->history[me->qhistory++] = step;
	return 0;
}

static int random_ai_do_steps(
	struct ai * restrict const ai,
	const unsigned int qsteps,
	const int steps[])
{
    ai->error = NULL;
    struct random_ai * restrict const me = ai->data;
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

static int random_ai_undo_step(struct ai * restrict const ai)
{
    ai->error = NULL;
    struct state * restrict const state = &ai->state;
    struct random_ai * restrict const me = ai->data;
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

static int random_ai_undo_steps(struct ai * restrict const ai, const unsigned int qsteps)
{
    ai->error = NULL;
    struct state * restrict const state = &ai->state;
    struct random_ai * restrict const me = ai->data;
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

static int random_ai_go(
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

static const struct ai_param * random_ai_get_params(const struct ai * const ai)
{
	static const struct ai_param terminator = { NULL, NULL, NO_TYPE, 0 };
	return &terminator;
}

static int random_ai_set_param(
	struct ai * restrict const ai,
	const char * const name,
	const void * const value)
{
	ai->error = "Unknown parameter name.";
	return EINVAL;
}

static void free_random_ai(struct ai * restrict const ai)
{
    struct random_ai * restrict const me = ai->data;
    free(me->dynamic_data);
    free(me->static_data);
}

int init_random_ai(
    struct ai * restrict const ai,
    const struct geometry * const geometry)
{
    ai->error = NULL;

    size_t static_data_sz = sizeof(struct random_ai);
    void * static_data = malloc(static_data_sz);
    if (static_data == NULL) {
        ai->error = "Cannot allocate static_data";
        return ENOMEM;
    }

    memset(static_data, 0, static_data_sz);
    struct random_ai * restrict const me = static_data;

    me->static_data = static_data;
    const int status = reset_dynamic(me, geometry);
    if (status != 0) {
        ai->error = "Cannot alocate dynamic data";
        free(static_data);
        return ENOMEM;
    }

    ai->data = me;

    ai->reset = random_ai_reset;
    ai->do_step = random_ai_do_step;
    ai->do_steps = random_ai_do_steps;
    ai->undo_step = random_ai_undo_step;
    ai->undo_steps = random_ai_undo_steps;
    ai->go = random_ai_go;
    ai->get_params = random_ai_get_params;
    ai->set_param = random_ai_set_param;
    ai->get_state = ai_get_state;
    ai->free = free_random_ai;

    struct state * restrict const state = &ai->state;
    init_state(state, geometry);
    return 0;
}
