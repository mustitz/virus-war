#include "hashes.h"
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
#define KW_HISTORY          7
#define KW_SET              8
#define KW_AI               9
#define KW_INFO            10
#define KW_GO              11
#define KW_TIME            12
#define KW_SCORE           13
#define KW_STEPS           14

#define ITEM(name) { #name, KW_##name }
struct keyword_desc keywords[] = {
    { "exit", KW_QUIT },
    ITEM(QUIT),
    ITEM(PING),
    ITEM(SRAND),
    ITEM(NEW),
    ITEM(STATUS),
    ITEM(STEP),
    ITEM(HISTORY),
    ITEM(SET),
    ITEM(AI),
    ITEM(INFO),
    ITEM(GO),
    ITEM(TIME),
    ITEM(SCORE),
    ITEM(STEPS),
    { NULL, 0 }
};

struct ai_desc
{
    const char * name;
    const char * sha512;
    int (*init_ai)(struct ai * restrict const ai, const struct geometry * const geometry);
};

const struct ai_desc ai_list[] = {
    { "random", RANDOM_AI_HASH, &init_random_ai },
    { "mcts", MCTS_AI_HASH, &init_mcts_ai },
    { NULL, NULL, NULL }
};

enum ai_go_flags { EXPLAIN_TIME, EXPLAIN_SCORE, EXPLAIN_STEPS };

struct cmd_parser
{
    struct line_parser line_parser;
    const struct keyword_tracker * tracker;

    int n;
    struct geometry * geometry;
    struct state * state;

    int qhistory;
    int * history;

    struct ai * ai;
    struct ai ai_storage;
    const struct ai_desc * ai_desc;
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

    me->ai = NULL;

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

    if (me->ai) {
        me->ai->free(me->ai);
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
        me->history = history;
    }

    me->n = n;
    me->qhistory = 0;
    init_state(me->state, me->geometry);

    struct ai * restrict const ai = me->ai;
    if (ai != NULL) {
        const int status = ai->reset(ai, me->geometry);
        if (status != 0) {
            fprintf(stderr, "AI crash: ai->reset(geometry) failed with code %d, %s.\n",
                status, strerror(status));
            me->ai = NULL;
            ai->free(ai);
            return;
        }
    }
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

static void set_ai(
    struct cmd_parser * restrict const me,
    const struct ai_desc * const ai_desc)
{
    struct ai storage;
    struct ai * restrict const ai = &storage;

    const int status = ai_desc->init_ai(ai, me->geometry);
    if (status != 0) {
        fprintf(stderr, "AI crash: cannot set AI, init failed with code %d, %s.\n",
            status, strerror(status));
        return;
    }

    if (me->qhistory > 0) {
        const int status = ai->do_steps(ai, me->qhistory, me->history);
        if (status != 0) {
            fprintf(stderr, "AI crash: cannot set AI, cannot apply history, status = %d, %s.\n",
                status, strerror(status));
            ai->free(ai);
            return;
        }
    }

    if (me->ai) {
        me->ai->free(me->ai);
    }

    me->ai_storage = *ai;
    me->ai = &me->ai_storage;
    me->ai_desc = ai_desc;
}

static void ai_info(struct cmd_parser * restrict const me)
{
    const struct ai * const ai = me->ai;
    if (ai == NULL) {
        fprintf(stderr, "No AI set, use “set ai [name]” command before.\n");
        return;
    }

    printf("%12s\t%12s\n", "name", me->ai_desc->name);
    printf("%12s\t%12.12s\n", "hash", me->ai_desc->sha512);

    const struct ai_param * ptr = me->ai->get_params(me->ai);
    for (; ptr->name != NULL; ++ptr) {
        switch (ptr->type) {
            case I32:
                printf("%12s\t%12d\n", ptr->name, *(int32_t*)ptr->value);
                break;
            case U32:
                printf("%12s\t%12u\n", ptr->name, *(uint32_t*)ptr->value);
                break;
            case F32:
                printf("%12s\t%12f\n", ptr->name, *(float*)ptr->value);
                break;
            default:
                break;
        }
    }
}

static void explain_step(
    const int sq,
    const int n,
    const unsigned int flags,
    const struct ai_explanation * const explanation)
{
    if (flags == 0) {
        return;
    }

    const unsigned int time_mask = 1 << EXPLAIN_TIME;
    const unsigned int score_mask = 1 << EXPLAIN_SCORE;
    const unsigned int step_mask = 1 << EXPLAIN_STEPS;

    const unsigned int line_mask = time_mask | score_mask;
    if (flags & line_mask) {
        const int rank = sq / n;
        const int file = sq % n;
        printf("  %c%-2d", FILE_CHARS[file], rank+1);
        if (flags & time_mask) {
            printf(" in %.3fs", explanation->time);
        }
        if (flags & score_mask) {
            const double score = explanation->score;
            if (score >= 0.0 && score <= 1.0) {
                printf(" score %5.1f%%", 100.0 * score);
            } else {
                printf(" score N/A");
            }
        }
        printf("\n");
    }

    if (flags & step_mask) {
        const struct step_stat * ptr = explanation->stats;
        const struct step_stat * const end = ptr + explanation->qstats;
        for (; ptr != end; ++ptr) {
            const int rank = ptr->square / n;
            const int file = ptr->square % n;
            printf("        %c%-2d", FILE_CHARS[file], rank+1);
            if (ptr->qgames > 0) {
                printf("%5.1f%% %6d\n", 100 * ptr->score, ptr->qgames);
            } else {
                printf("    N/A    N/A\n");
            }
        }
    }
}

int ai_play(
    struct cmd_parser * restrict const me,
    const unsigned int flags)
{
    struct state * restrict const state = me->state;
    struct ai * restrict const ai = me->ai;
    const int n = me->n;
    const int active = state->active;

    for (;;) {
        struct ai_explanation explanation;
        const int step = ai->go(ai, flags ? &explanation : NULL);
        if (step < 0) {
            fprintf(stderr, "AI crash: ai->go() failed with code %d, %s.\n",
                errno, strerror(errno));
            return errno != 0 ? errno : EINVAL;
        }

        const int ai_status = ai->do_step(ai, step);
        if (ai_status != 0) {
            fprintf(stderr, "AI crash: ai->step(%d) failed with code %d, %s.\n",
                step, ai_status, strerror(ai_status));
            return ai_status;
        }

        const int status = state_step(me->state, step);
        if (status != 0) {
            fprintf(stderr, "AI crash: ai->go() returned impossible move, state_step(%d) failed with code %d, %s.\n",
                step, status, strerror(status));
            return status;
        }

        me->history[me->qhistory++] = step;

        if (flags) {
            explain_step(step, n, flags, &explanation);
        }

        if (state->active != active) {
            return 0;
        }

        if (state_status(state) != 0) {
            return 0;
        }
    }
}

void ai_go(
    struct cmd_parser * restrict const me,
    const unsigned int flags)
{
    if (state_status(me->state) != 0) {
        fprintf(stderr, "Game over, no moves possible.\n");
        return;
    }

    struct ai * restrict const ai = me->ai;
    if (ai == NULL) {
        fprintf(stderr, "No AI set, use “set ai [name]” command before.\n");
        return;
    }

    struct state backup = *me->state;
    const int saved_qhistory = me->qhistory;
    const int status = ai_play(me, flags);
    if (status != 0){
        *me->state = backup;
        me->qhistory = saved_qhistory;
        ai->free(ai);
        me->ai = NULL;
        return;
    }

    const int n = me->n;
    const int * step_ptr = me->history + saved_qhistory;
    const int * const end = me->history + me->qhistory;
    const char * separator = "";
    for (; step_ptr != end; step_ptr++) {
        const int sq = *step_ptr;
        const int rank = sq / n;
        const int file = sq % n;
        printf("%s%c%d", separator, FILE_CHARS[file], rank+1);
        separator = " ";
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
    const char * const chars = ".OX??xo?";
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
        return;
    }

    struct ai * restrict const ai = me->ai;
    if (ai != NULL) {
        const int qsteps = me->qhistory - saved_qhistory;
        const int * const steps = me->history + saved_qhistory;
        const int status = ai->do_steps(ai, qsteps, steps);
        if (status != 0) {
            fprintf(stderr, "AI crash: ai->do_steps(%d, steps) failed with code %d, %s.\n",
                qsteps, status, strerror(status));
            me->ai = NULL;
            ai->free(ai);
            return;
        }
    }
}

void process_history(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    if (!parser_check_eol(lp)) {
        error(lp, "End of line expected (HISTORY parsed), but something was found.");
        return;
    }

    if (me->qhistory == 0) {
        return;
    }

    const int n = me->n;
    const char * separator = "";
    for (int i=0; i<me->qhistory; ++i) {
        const int sq = me->history[i];
        const int rank = sq / n;
        const int file = sq % n;
        printf("%s%c%d", separator, FILE_CHARS[file], rank+1);
        separator = " ";
    }

    printf("\n");
}

void process_set_ai(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    parser_skip_spaces(lp);

    if (parser_check_eol(lp)) {
        const struct ai_desc * restrict ptr = ai_list;
        for (; ptr->name; ++ptr) {
            printf("%s\n", ptr->name);
        }
        return;
    }

    const unsigned char * const ai_name = lp->current;
    const int status = parser_read_id(lp);
    if (status != 0) {
        error(lp, "Invalid AI name, valid identifier expected.");
        return;
    }
    const size_t len = lp->current - ai_name;

    if (!parser_check_eol(lp)) {
        error(lp, "End of line expected but something was found in SET AI command.");
        return;
    }

    const struct ai_desc * restrict ptr = ai_list;
    for (; ptr->name; ++ptr) {
        const int match = 1
            && strlen(ptr->name) == len
            && strncasecmp(ptr->name, (const char *)ai_name, len) == 0
        ;

        if (match) {
            return set_ai(me, ptr);
        }
    }

    lp->lexem_start = ai_name;
    error(lp, "AI not found.");
}

void process_set(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    const int keyword = read_keyword(me);

    if (keyword == -1) {
        error(lp, "Invalid lexem in SET command.");
        return;
    }

    switch (keyword) {
        case KW_AI:
            return process_set_ai(me);
    }

    error(lp, "Invalid option name in SET command.");
}

void process_ai_info(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    if (!parser_check_eol(lp)) {
        error(lp, "End of line expected (AI INFO command is parsed), but someting was found.");
        return;
    }

    ai_info(me);
}

void process_ai_go(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;

    unsigned int flags = 0;
    while (!parser_check_eol(lp)) {
        const int keyword = read_keyword(me);
        if (keyword == -1) {
            error(lp, "Invalid lexem in AI GO command.");
            return;
        }

        switch (keyword) {
            case KW_TIME:
                flags |= 1 << EXPLAIN_TIME;
                break;
            case KW_SCORE:
                flags |= 1 << EXPLAIN_SCORE;
                break;
            case KW_STEPS:
                flags |= 1 << EXPLAIN_STEPS;
                break;
            default:
                error(lp, "Invalid explain flag in AI GO command.");
                return;
        }

        parser_skip_spaces(lp);
        if (lp->current[0] == '|' || lp->current[0] == ',') {
            ++lp->current;
            parser_skip_spaces(lp);
        }
    }

    ai_go(me, flags);
}

void process_ai(struct cmd_parser * restrict const me)
{
    struct line_parser * restrict const lp = &me->line_parser;
    const int keyword = read_keyword(me);

    if (keyword == -1) {
        error(lp, "Invalid lexem in AI command.");
        return;
    }

    switch (keyword) {
        case KW_INFO:
            return process_ai_info(me);
        case KW_GO:
            return process_ai_go(me);
    }

    error(lp, "Invalid action in AI command.");
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
        case KW_HISTORY:
            process_history(me);
            break;
        case KW_SET:
            process_set(me);
            break;
        case KW_AI:
            process_ai(me);
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
