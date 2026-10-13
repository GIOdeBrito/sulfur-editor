#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>

#include "buffer.h"
#include "editor.h"
#include "input.h"
#include "screen.h"

int main(int argc, char *argv[])
{
	const char *filename;

	if(argc > 2)
	{
		fprintf(stderr, "Usage: %s [filename]\n", argv[0]);
		return EXIT_FAILURE;
	}

	filename = NULL;

	if(argc == 2)
	{
		filename = argv[1];
	}

	editor_init();
	editor_open(filename);
	editor_start_screen();
	editor_run();
	endwin();
	editor_free();
	return EXIT_SUCCESS;
}
