#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>

#include "screen.h"
#include "util.h"

void die(const char *msg)
{
	editor_end_screen();
	perror(msg);
	exit(EXIT_FAILURE);
}
