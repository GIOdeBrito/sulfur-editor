#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"
#include "common.h"
#include "editor.h"
#include "input.h"
#include "screen.h"

static void print_usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [filename]\n", prog);
}

static void print_help(const char *prog)
{
	printf("Usage: %s [filename]\n", prog);
	printf("\n");
	printf("sulf %s - a small terminal text editor.\n", SULF_VERSION);
	printf("\n");
	printf("Keys:\n");
	printf("  Arrows / Home / End / PgUp / PgDn   Move (Ctrl-A / Ctrl-E also Home / End)\n");
	printf("  Shift+Arrows (and friends)          Extend selection\n");
	printf("  Type / Enter / Backspace / Del      Edit (Tab indents, Shift-Tab unindents)\n");
	printf("  Ctrl-S                              Save (prompts for a name if unnamed)\n");
	printf("  Ctrl-Q                              Quit (prompts if unsaved changes)\n");
	printf("  Ctrl-C / Ctrl-X / Ctrl-V            Copy / cut / paste selection\n");
	printf("  Ctrl-F, then n / N / Enter          Find next / previous match, Esc cancels\n");
	printf("  Ctrl-P / F1                         Command palette, Esc cancels\n");
	printf("  F2                                  File explorer, Esc cancels\n");
	printf("  Ctrl-Z / Ctrl-Y                     Undo / redo\n");
	printf("  Ctrl-Space                          Force completion (Tab / Enter accept)\n");
	printf("  Mouse click / wheel                 Move cursor / scroll\n");
}

static void print_version(void)
{
	printf("sulf %s\n", SULF_VERSION);
}

static int handle_flag(const char *arg, const char *prog)
{
	if(strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0)
	{
		print_help(prog);
		return EXIT_SUCCESS;
	}

	if(strcmp(arg, "--version") == 0 || strcmp(arg, "-V") == 0)
	{
		print_version();
		return EXIT_SUCCESS;
	}

	if(arg[0] == '-' && arg[1] != '\0')
	{
		fprintf(stderr, "Unknown option: %s\n", arg);
		print_usage(prog);
		return EXIT_FAILURE;
	}

	return -1;
}

int main(int argc, char *argv[])
{
	const char *filename;
	int flag_result;

	if(argc > 2)
	{
		print_usage(argv[0]);
		return EXIT_FAILURE;
	}

	filename = NULL;

	if(argc == 2)
	{
		flag_result = handle_flag(argv[1], argv[0]);

		if(flag_result >= 0)
		{
			return flag_result;
		}

		filename = argv[1];
	}

	editor_init();
	editor_open(filename);
	editor_start_screen();
	editor_run();
	editor_end_screen();
	editor_free();
	return EXIT_SUCCESS;
}
