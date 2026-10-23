#include <locale.h>
#include <ncurses.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include "focus.h"
#include "highlight.h"
#include "mouse.h"
#include "screen.h"

static void editor_init_locale(void)
{
	setlocale(LC_ALL, "");

	if(MB_CUR_MAX >= 4)
	{
		return;
	}

	if(setlocale(LC_ALL, "C.UTF-8") == NULL)
	{
		setlocale(LC_ALL, "C.utf8");
	}
}

void editor_disable_flow_control(void)
{
	struct termios t;

	if(tcgetattr(STDIN_FILENO, &t) != 0)
	{
		return;
	}

	t.c_iflag &= (tcflag_t)~(IXON | IXOFF);
	t.c_cc[VSTART] = (cc_t)_POSIX_VDISABLE;
	t.c_cc[VSTOP] = (cc_t)_POSIX_VDISABLE;

	tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

void editor_start_screen(void)
{
	editor_init_locale();
	initscr();
	raw();
	keypad(stdscr, TRUE);
	noecho();
	intrflush(stdscr, FALSE);
	set_escdelay(25);
	editor_disable_flow_control();
	highlight_init_color();
	mouse_init();
	focus_init();
}

void editor_end_screen(void)
{
	focus_shutdown();
	endwin();
}
