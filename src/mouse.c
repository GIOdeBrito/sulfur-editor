#include <ncurses.h>

#include "common.h"
#include "complete.h"
#include "editor.h"
#include "mouse.h"
#include "view.h"

#define MOUSE_WHEEL_STEP 3

static int s_enabled;

void mouse_init(void)
{
	mmask_t result;

	s_enabled = 0;
	result = mousemask((mmask_t)(BUTTON1_PRESSED | BUTTON1_DOUBLE_CLICKED | BUTTON4_PRESSED | BUTTON5_PRESSED), NULL);

	if(result == 0)
	{
		return;
	}

	s_enabled = 1;
}

static void mouse_scroll(int direction)
{
	int view_rows;
	size_t max_off;

	view_rows = LINES - 2;

	if(view_rows < 1)
	{
		view_rows = 1;
	}

	max_off = 0;

	if(g_editor.num_lines > (size_t)view_rows)
	{
		max_off = g_editor.num_lines - (size_t)view_rows;
	}

	if(direction < 0)
	{
		if(g_editor.row_off <= (size_t)MOUSE_WHEEL_STEP)
		{
			g_editor.row_off = 0;
		}
		else
		{
			g_editor.row_off -= (size_t)MOUSE_WHEEL_STEP;
		}
	}
	else
	{
		g_editor.row_off += (size_t)MOUSE_WHEEL_STEP;

		if(g_editor.row_off > max_off)
		{
			g_editor.row_off = max_off;
		}
	}

	if(g_editor.cy < g_editor.row_off)
	{
		g_editor.cy = g_editor.row_off;
	}

	if(g_editor.cy >= g_editor.row_off + (size_t)view_rows)
	{
		g_editor.cy = g_editor.row_off + (size_t)view_rows - 1;
	}

	editor_clamp_cursor();
	g_editor.want_cx = g_editor.cx;
}

static int mouse_click(MEVENT *ev)
{
	int text_off;
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

	file_row = g_editor.row_off + (size_t)ev->y;

	if(file_row >= g_editor.num_lines)
	{
		return 0;
	}

	text_off = editor_text_offset();
	g_editor.cy = file_row;
	complete_reset();

	if(ev->x <= text_off)
	{
		g_editor.cx = 0;
		g_editor.want_cx = 0;
		return 1;
	}

	want = g_editor.col_off + (size_t)(ev->x - text_off);
	g_editor.cx = want;
	editor_clamp_cursor();
	g_editor.want_cx = g_editor.cx;
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

	if((ev.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED)) != 0)
	{
		return mouse_click(&ev);
	}

	return 0;
}
