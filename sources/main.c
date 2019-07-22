#include "virus-war.h"
#include "parser.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define KW_QUIT             1
#define KW_PING             2
#define KW_SRAND            3
#define KW_NEW              4

#define ITEM(name) { #name, KW_##name }
struct keyword_desc keywords[] = {
    { "exit", KW_QUIT },
    ITEM(QUIT),
    ITEM(PING),
    ITEM(SRAND),
    ITEM(NEW),
    { NULL, 0 }
};

struct cmd_parser
{
    struct line_parser line_parser;
    const struct keyword_tracker * tracker;

    int n;
    struct geometry * geometry;
    struct state * state;
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
        destroy_geometry(me->geometry);
        me->geometry = geometry;
    }

    me->n = n;
    init_state(me->state, me->geometry);
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
