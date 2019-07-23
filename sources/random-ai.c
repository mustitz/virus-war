#include "virus-war.h"

static int random_ai_reset(
	struct ai * restrict const ai,
	const struct geometry * const geometry)
{
	ai->error = "Not implemented.";
	return EINVAL;
}

static int random_ai_do_step(
	struct ai * restrict const ai,
	const int step)
{
	ai->error = "Not implemented.";
	return EINVAL;
}

static int random_ai_do_steps(
	struct ai * restrict const ai,
	const unsigned int qsteps,
	const int steps[])
{
	ai->error = "Not implemented.";
	return EINVAL;
}

static int random_ai_undo_step(struct ai * restrict const ai)
{
	ai->error = "Not implemented.";
	return EINVAL;
}

static int random_ai_undo_steps(struct ai * restrict const ai, const unsigned int qsteps)
{
	ai->error = "Not implemented.";
	return EINVAL;
}

static int random_ai_go(
	struct ai * restrict const ai,
	struct ai_explanation * restrict const explanation)
{
	ai->error = "Not implemented.";
    errno = EINVAL;
	return -1;
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

static const struct state * random_ai_get_state(const struct ai * const ai)
{
	return NULL;
}

static void free_random_ai(struct ai * restrict const ai)
{
}

int init_random_ai(
    struct ai * restrict const ai,
    const struct geometry * const geometry)
{
    ai->error = NULL;
    ai->data = NULL;

    ai->reset = random_ai_reset;
    ai->do_step = random_ai_do_step;
    ai->do_steps = random_ai_do_steps;
    ai->undo_step = random_ai_undo_step;
    ai->undo_steps = random_ai_undo_steps;
    ai->go = random_ai_go;
    ai->get_params = random_ai_get_params;
    ai->set_param = random_ai_set_param;
    ai->get_state = random_ai_get_state;
    ai->free = free_random_ai;

    return 0;
}
