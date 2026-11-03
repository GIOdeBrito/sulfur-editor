#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"
#include "common.h"
#include "config.h"
#include "editor.h"
#include "input.h"
#include "screen.h"
#include "tabs.h"

static void print_usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [file ...]\n", prog);
}

static void print_help(const char *prog)
{
	printf("Usage: %s [file ...]\n", prog);
	printf("\n");
	printf("sulf %s - a small terminal text editor.\n", SULF_VERSION);
	printf("\n");
	printf("Each file opens in its own tab (top row, click or Ctrl-]/Ctrl-^ to switch).\n");
	printf("\n");
	printf("Keys:\n");
	printf("  Arrows / Home / End / PgUp / PgDn   Move (Ctrl-A / Ctrl-E also Home / End)\n");
	printf("  Shift+Arrows (and friends)          Extend selection\n");
	printf("  Type / Enter / Backspace / Del      Edit (Tab indents, Shift-Tab unindents)\n");
	printf("  Ctrl-S                              Save (prompts for a name if unnamed)\n");
	printf("  Ctrl-Q                              Quit (prompts per unsaved buffer)\n");
	printf("  Ctrl-] / Ctrl-^                     Next / previous buffer tab\n");
	printf("  Ctrl-C / Ctrl-X / Ctrl-V            Copy / cut / paste selection\n");
	printf("  Ctrl-F, then n / N / Enter          Find next / previous match, Esc cancels\n");
	printf("  Ctrl-P / F1                         Command palette, Esc cancels\n");
	printf("  F2                                  File explorer, Esc cancels\n");
	printf("  Ctrl-Z / Ctrl-Y                     Undo / redo\n");
  printf("  Ctrl-Space                          Force completion (Tab / Enter accept)\n");
  printf("  Mouse click / wheel                 Move cursor / scroll / switch tab\n");
  printf("\n");
  printf("Config: $XDG_CONFIG_HOME/sulf/config, else ~/.config/sulf/config\n");
  printf("  (legacy ~/.config/sulf also works). Keys are tab_width, theme,\n");
  printf("  color.<class>, and key.<action>; see README.\n");
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
	int i;
	int opened;
	int first_tab;

	opened = 0;
	first_tab = -1;
	tabs_init();
	config_load();

	for(i = 1; i < argc; i++)
	{
		int flag_result;

		flag_result = handle_flag(argv[i], argv[0]);

		if(flag_result >= 0)
		{
			tabs_free();
			return flag_result;
		}

		if(tabs_open_file(argv[i]) == NULL)
		{
			break;
		}

		if(first_tab < 0)
		{
			first_tab = tabs_active_index();
		}

		opened++;
	}

	/* No files: tabs_init already prepared a fresh unnamed buffer. */
	if(first_tab >= 0)
	{
		tabs_activate(first_tab);
	}

	editor_start_screen();

	if(config_notice()[0] != '\0')
	{
		editor_set_status(config_notice());
	}

	editor_run();
	editor_end_screen();
	tabs_free();
	return EXIT_SUCCESS;
}
