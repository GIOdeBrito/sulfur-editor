#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"

void die(const char *msg)
{
	endwin();
	perror(msg);
	exit(EXIT_FAILURE);
}
