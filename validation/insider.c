#include "insider.h"
#include "virus-war.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

const char * test_name = "";

void fail(void)
{
    fprintf(stderr, "\n");
    exit(1);
}

void test_fail(const char * const fmt, ...)
{
    fprintf(stderr, "Test `%s' fails: ", test_name);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fail();
}



/* Tests */

int test_empty(void)
{
    return 0;
}



/* Run/list tests */

typedef int (* test_function)(void);

struct test_item
{
    const char * name;
    test_function function;
};

const struct test_item tests[] = {
    { "empty", &test_empty },
    { "nn-simulate", &test_nn_simulate },
    { "nn-rollout", &test_nn_rollout },
    { "nn", &test_nn },
    { "all-3moves", &test_all_3moves },
    { "get-3moves-3", &test_get_3moves_3 },
    { "get-3moves-2", &test_get_3moves_2 },
    { "get-3moves-1", &test_get_3moves_1 },
    { "get-3moves-0", &test_get_3moves_0 },
    { "simulate", &test_simulate },
    { "mcts-init-free", &test_mcts_init_free },
    { "allocn", &test_allocn },
    { "multiallocator", &test_multiallocator },
    { "rollout", &test_rollout },
    { "random-ai", &test_random_ai },
    { "unstep", &test_unstep },
    { "nth-one-index", &test_nth_one_index },
    { "first-one", &test_first_one },
    { "game", &test_game },
    { "calc-next-steps", &test_calc_next_steps},
    { "next-steps", &test_next_steps },
    { "grow", &test_grow },
    { "popcount", &test_popcount },
    { "state", &test_state },
    { "geometry", &test_geometry },
    { "parser", &test_parser },
    { "multialloc", &test_multialloc },
    { NULL, NULL }
};

void print_tests(void)
{
    const struct test_item * current = tests;
    for (; current->name != NULL; ++current) {
        printf("%s\n", current->name);
    }
}

void run_test_item(const struct test_item * const item)
{
    test_name = item->name;
    printf("Run test for %s:\n", item->name);
    const int test_exit_code = (*item->function)();
    if (test_exit_code) {
        exit(test_exit_code);
    }
}

void run_all_tests(void)
{
    const struct test_item * current = tests;
    for (; current->name != NULL; ++current) {
        run_test_item(current);
    }
}

void run_test(const char * const name)
{
    if (strcmp(name, "all") == 0) {
        return run_all_tests();
    }

    const struct test_item * current = tests;
    for (; current->name != NULL; ++current) {
        if (strcmp(name, current->name) == 0) {
            return run_test_item(current);
        }
    }

    fprintf(stderr, "Test “%s” is not found.", name);
    fail();
}

int main(const int argc, const char * const argv[])
{
    if (argc == 1) {
        print_tests();
        return 0;
    }

    for (size_t i=1; i<argc; ++i) {
        run_test(argv[i]);
    }

    return 0;
}
