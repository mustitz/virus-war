bin_PROGRAMS = virus-war



if DEBUG_MODE
EXTRA_CFLAGS = -g3 -O0 -Wall -Werror
else
EXTRA_CFLAGS = -Ofast
endif



virus_war_CFLAGS = $(EXTRA_CFLAGS)
virus_war_SOURCES = main.c game.c random-ai.c parser.c utils.c
