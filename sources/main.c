#include "virus-war.h"
#include "parser.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define FILE_CHARS "abcdefghiklmnoprst"

#define KW_QUIT             1
#define KW_PING             2
#define KW_SRAND            3
#define KW_NEW              4
#define KW_STATUS           5
#define KW_STEP             6

#define ITEM(name) { #name, KW_##name }
struct keyword_desc keywords[] = {
    { "exit", KW_QUIT },
    ITEM(QUIT),
    ITEM(PING),
    ITEM(SRAND),
    ITEM(NEW),
    ITEM(STATUS),
    ITEM(STEP),
    { NULL, 0 }
};

struct cmd_parser
{
    struct line_parser line_parser;
    const struct keyword_tracker * tracker;

    int n;
    struct geometry * geometry;
    struct state * state;

    int qhistory;
    int * history;
};



static void error(struct line_parser * restrict const lp, const char * fmt, ...) __attribute__ ((format (printf, 2, 3)));

static void error(struct line_parser * restrict const lp, const char * fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "Parsing error: ");
    vfprintf(stderr, fmt, args);
    va_end(args);

    int offset = lp->lexem_start - lp->line;
    fprintf(stderr, "\n> %s> %*s^\n", lp->line, offset, "");
}

static int read_keyword(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    parser_skip_spaces(lp);
    return parser_read_keyword(lp, me->tracker);
}



int init_cmd_parser(struct cmd_parser * restrict const me)
{
    void free_cmd_parser(const struct cmd_parser * const me);

    me->tracker = NULL;

    me->n = 10;
    me->geometry = NULL;
    me->state = NULL;

    me->tracker = create_keyword_tracker(keywords, KW_TRACKER__IGNORE_CASE);
    if (me->tracker == NULL) {
        free_cmd_parser(me);
        return ENOMEM;
    }

    me->geometry = create_std_geometry(me->n);
    if (me->geometry == NULL) {
        free_cmd_parser(me);
        return ENOMEM;
    }

    me->state = create_state(me->geometry);
    if (me->state == NULL) {
        free_cmd_parser(me);
        return ENOMEM;
    }

    const size_t max_history = me->n * me->n * 2;
    const size_t sz = max_history * sizeof(int);
    me->qhistory = 0;
    me->history = malloc(sz);
    if (me->history == NULL) {
        free_cmd_parser(me);
        return ENOMEM;
    }

    return 0;
}

void free_cmd_parser(const struct cmd_parser * const me)
{
    if (me->state != NULL) {
        destroy_state(me->state);
    }

    if (me->geometry != NULL) {
        destroy_geometry(me->geometry);
    }

    if (me->tracker != NULL) {
        destroy_keyword_tracker(me->tracker);
    }

    if (me->history) {
        free(me->history);
    }
}

void new_game(struct cmd_parser * restrict const me, const int n)
{
    const int new_geometry = n != me->geometry->n;

    if (new_geometry) {
        struct geometry * restrict const geometry = create_std_geometry(n);
        if (geometry == NULL) {
            fprintf(stderr, "Error: create_std_geometry fails with code %d: %s\n", errno, strerror(errno));
            return;
        }

        const size_t max_history = n * n * 2;
        const size_t sz = max_history * sizeof(int);
        int * restrict const history = realloc(me->history, sz);
        if (history == NULL) {
            fprintf(stderr, "Error: realloc(history, %lu) fails.\n", sz);
            destroy_geometry(geometry);
            return;
        }

        destroy_geometry(me->geometry);
        me->geometry = geometry;
    }

    me->n = n;
    me->qhistory = 0;
    init_state(me->state, me->geometry);
}

void print_steps(const struct state * const me)
{
    const bb_t steps = state_get_steps(me);
    if (steps == 0) {
        return;
    }

    const char * separator = "";
    const int n = me->geometry->n;
    for (int sq=0; sq<n*n; ++sq) {
        const bb_t bb = BB_SQUARE(sq);
        if (bb & steps) {
            const int rank = sq / n;
            const int file = sq % n;
            printf("%s%c%d", separator, FILE_CHARS[file], rank+1);
            separator = " ";
        }
    }
    printf("\n");
}

int process_quit(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    if (!parser_check_eol(lp)) {
        error(lp, "End of line expected (QUIT command is parsed), but someting was found.");
        return 0;
    }
    return 1;
}

void process_srand(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    if (parser_check_eol(lp)) {
        srand(time(NULL));
        return;
    }

    int value;
    const int status = parser_read_last_int(lp, &value);
    if (status != 0) {
        error(lp, "Integer constant or EOL expected in SRAND command.");
        return;
    }

    srand((unsigned int)value);
}

void process_new(struct cmd_parser * restrict const me)
{
    int n = me->geometry->n;

    struct line_parser * restrict const lp = &me->line_parser;

    if (!parser_check_eol(lp)) {
        const unsigned char * const lexem = lp->lexem_start;
        const int status = parser_read_last_int(lp, &n);
        if (status != 0) {
            error(lp, "Board size (integer constant in range 3..11) or EOL expected in NEW command.");
            return;
        }

        if (n <= 2) {
            lp->lexem_start = lexem;
            error(lp, "Board size too small, it might be at least 3.");
            return;
        }

        if (n > 11) {
            lp->lexem_start = lexem;
            error(lp, "Board size too large, maximum value is 11.");
            return;
        }
    }

    new_game(me, n);
}

static const char * active_str(const struct state * const me)
{
    switch (me->active) {
        case 1: return "X";
        case 2: return "O";
        case 0: return "none";
        default: return "???";
    }
}

static const char * status_str(const struct state * const me)
{
    const int status = state_status(me);
    switch (status) {
        case 1: return "X";
        case 2: return "O";
        case 0: return "in progress";
        default: return "???";
    }
}

static int get_ch(const int is_x, const int is_o, const int is_dead)
{
    const int index = is_dead *4 + is_x * 2 + is_o;
    const char * const chars = ".OX??ox?";
    return chars[index];
}

void process_status(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    if (!parser_check_eol(lp)) {
        error(lp, "End of line expected (STATUS parsed), but something was found.");
        return;
    }

    const int indent = 2;
    const int param_len = -8;
    const struct state * const state = me->state;

    printf("%*s%*s %s\n", indent, "", param_len, "Active:", active_str(state));

    const int qsteps = pop_count(state->x | state->o) + pop_count(state->dead);
    const int move_num = (qsteps / 6) + 1;
    const int step_num = (qsteps % 3) + 1;
    printf("%*s%*s move %d, step %d\n", indent, "", param_len, "Move:", move_num, step_num);

    printf("%*s%*s %s\n", indent, "", param_len, "Status:", status_str(state));

    printf("%*s%*s\n", indent, "", param_len, "Board:");

    bb_t steps = state_get_steps(state);
    const int n = me->n;
    for (int rank = n-1; rank >= 0; --rank) {
        printf("%*s%2d | ", 2*indent, "", rank+1);
        int is_green = 0;
        for (int file = 0; file < n; ++file) {
            const int bit_index = n * rank + file;
            const bb_t bb = BB_SQUARE(bit_index);
            if (bb & steps) {
                if (!is_green) {
                    printf("\033[0;32m");
                    is_green = 1;
                }
            } else {
                if (is_green) {
                    printf("\033[0m");
                    is_green = 0;
                }
            }
            const int is_x = (bb & state->x) != 0;
            const int is_o = (bb & state->o) != 0;
            const int is_dead = (bb & state->dead) != 0;
            printf("%c", get_ch(is_x, is_o, is_dead));
        }
        if (is_green) {
            printf("\033[0m");
        }
        printf("\n");
    }
    printf("%*s---+-%*.*s\n", 2*indent, "", n, n, "------------------");
    printf("%*s   | %*.*s\n", 2*indent, "", n, n, FILE_CHARS);
}

int process_steps(struct cmd_parser * restrict const me)
{
    const int n = me->n;
    struct line_parser * restrict const lp = &me->line_parser;

    while (!parser_check_eol(lp)) {
        parser_skip_spaces(lp);
        const int ch = lp->current[0];
        const char * ptr = strchr(FILE_CHARS, ch);
        if (ptr == NULL) {
            error(lp, "Invalid square file.");
            return EINVAL;
        }
        const int file = ptr - FILE_CHARS;
        if (file >= n) {
            error(lp, "Invalid square file.");
            return EINVAL;
        }
        ++lp->current;

        int printable_rank;
        const int parser_status = parser_try_int(lp, &printable_rank);
        if (parser_status != 0) {
            error(lp, "Invalid square rank.");
            return EINVAL;
        }
        const int rank = printable_rank - 1;
        if (rank >= n) {
            error(lp, "Invalid square rank.");
            return EINVAL;
        }

        const int step = rank*n + file;
        const int step_status = state_step(me->state, step);
        if (step_status != 0) {
            error(lp, "Impossible step.");
            return EINVAL;
        }

        me->history[me->qhistory++] = step;
    }

    return 0;
}

void process_step(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    if (parser_check_eol(lp)) {
        print_steps(me->state);
        return;
    }

    struct state backup = *me->state;
    const int saved_qhistory = me->qhistory;
    const int status = process_steps(me);
    if (status != 0){
        *me->state = backup;
        me->qhistory = saved_qhistory;
    }
}

int process_cmd(struct cmd_parser * restrict const me, const char * const line)
{
    struct line_parser * restrict const lp = &me->line_parser;
    parser_set_line(lp, line);

    if (parser_check_eol(lp)) {
        return 0;
    }

    const int keyword = read_keyword(me);
    if (keyword == -1) {
        error(lp, "Invalid lexem at the begginning of the line.");
        return 0;
    }

    if (keyword == 0) {
        error(lp, "Invalid keyword at the begginning of the line.");
        return 0;
    }

    if (keyword == KW_QUIT) {
        return process_quit(me);
    }

    switch (keyword) {
        case KW_PING:
            printf("pong%s", lp->current);
            fflush(stdout);
            fflush(stderr);
            break;
        case KW_SRAND:
            process_srand(me);
            break;
        case KW_NEW:
            process_new(me);
            break;
        case KW_STATUS:
            process_status(me);
            break;
        case KW_STEP:
            process_step(me);
            break;
        default:
            error(lp, "Unexpected keyword at the begginning of the line.");
            break;
    }

    return 0;
}

int main()
{
    struct cmd_parser cmd_parser;
    init_cmd_parser(&cmd_parser);

    char * line = 0;
    size_t len = 0;
    for (;; ) {
        const ssize_t has_read = getline(&line, &len, stdin);
        if (has_read == -1) {
            break;
        }

        const int is_quit = process_cmd(&cmd_parser, line);
        if (is_quit) {
            break;
        }
    }

    free_cmd_parser(&cmd_parser);

    if (line) {
        free(line);
    }

    return 0;
}
