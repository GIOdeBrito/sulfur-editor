#include <ncurses.h>

#include "common.h"
#include "complete.h"
#include "editor.h"
#include "mouse.h"
#include "search.h"
#include "view.h"

#define MOUSE_WHEEL_STEP 3

static int s_enabled;

void mouse_init(void)
{
	mmask_t result;

	s_enabled = 0;
	result = mousemask((mmask_t)(BUTTON1_PRESSED | BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED | BUTTON4_PRESSED | BUTTON5_PRESSED), NULL);

	if(result == 0)
	{
		return;
	}

	mouseinterval(0);
	s_enabled = 1;
}

static void mouse_scroll(int direction)
{
	size_t max_cy;

	max_cy = editor_max_cy();

	if(direction < 0)
	{
		if(g_editor.cy <= (size_t)MOUSE_WHEEL_STEP)
		{
			g_editor.cy = 0;
		}
		else
		{
			g_editor.cy -= (size_t)MOUSE_WHEEL_STEP;
		}
	}
	else
	{
		g_editor.cy += (size_t)MOUSE_WHEEL_STEP;

		if(g_editor.cy > max_cy)
		{
			g_editor.cy = max_cy;
		}
	}

	if(g_editor.cy >= g_editor.num_lines)
	{
		g_editor.cx = 0;
	}
	else
	{
		g_editor.cx = g_editor.want_cx;
	}

	editor_select_clear();
	editor_clamp_cursor();
	editor_scroll();

	if(g_editor.cy < g_editor.num_lines)
	{
		g_editor.want_cx = g_editor.cx;
	}
}

static int mouse_click(MEVENT *ev)
{
	int text_off;
	int map_w;
	size_t file_row;
	size_t want;

	if(ev == NULL)
	{
		return 0;
	}

	if(ev->y < 0 || ev->y >= LINES - 2)
	{
		return 0;
	}

	if(g_editor.num_lines == 0)
	{
		return 0;
	}

	map_w = editor_map_width();

	if(map_w > 0 && ev->x >= COLS - map_w)
	{
		size_t target;

		target = editor_map_bucket_start(ev->y);

		if(target >= g_editor.num_lines)
		{
			return 0;
		}

		g_editor.cy = target;
		editor_select_clear();
		complete_reset();
		search_deactivate();
		editor_clamp_cursor();
		g_editor.want_cx = g_editor.cx;
		return 1;
	}

	file_row = g_editor.row_off + (size_t)ev->y;

	if(file_row >= g_editor.num_lines)
	{
		return 0;
	}

	text_off = editor_text_offset();
	g_editor.cy = file_row;

	if(ev->x <= text_off)
	{
		g_editor.cx = 0;
	}
	else
	{
		want = g_editor.col_off + (size_t)(ev->x - text_off);
		g_editor.cx = want;
	}

	editor_clamp_cursor();
	g_editor.want_cx = g_editor.cx;
	editor_select_clear();
	complete_reset();
	search_deactivate();
	return 1;
}

int mouse_handle(void)
{
	MEVENT ev;

	if(s_enabled == 0)
	{
		return 0;
	}

	if(getmouse(&ev) != OK)
	{
		return 1;
	}

	if((ev.bstate & BUTTON4_PRESSED) != 0)
	{
		mouse_scroll(-1);
		return 1;
	}

	if((ev.bstate & BUTTON5_PRESSED) != 0)
	{
		mouse_scroll(1);
		return 1;
	}

	if((ev.bstate & BUTTON1_PRESSED) != 0)
	{
		return mouse_click(&ev);
	}

	if((ev.bstate & BUTTON1_CLICKED) != 0)
	{
		return mouse_click(&ev);
	}

	if((ev.bstate & BUTTON1_DOUBLE_CLICKED) != 0)
	{
		return mouse_click(&ev);
	}

	return 0;
}
