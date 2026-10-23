#include <ncurses.h>

#include "common.h"
#include "complete.h"
#include "editor.h"
#include "mouse.h"
#include "search.h"
#include "tabs.h"
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

static void mouse_scroll(Buffer *b, View *v, int direction)
{
	size_t max_cy;

	if(b == NULL || v == NULL)
	{
		return;
	}

	max_cy = editor_max_cy(b);

	if(direction < 0)
	{
		if(v->cy <= (size_t)MOUSE_WHEEL_STEP)
		{
			v->cy = 0;
		}
		else
		{
			v->cy -= (size_t)MOUSE_WHEEL_STEP;
		}
	}
	else
	{
		v->cy += (size_t)MOUSE_WHEEL_STEP;

		if(v->cy > max_cy)
		{
			v->cy = max_cy;
		}
	}

	if(v->cy >= b->num_lines)
	{
		v->cx = 0;
	}
	else
	{
		v->cx = v->want_cx;
	}

	editor_select_clear(v);
	editor_clamp_cursor(b, v);
	editor_scroll(b, v);

	if(v->cy < b->num_lines)
	{
		v->want_cx = v->cx;
	}
}

static int mouse_click(Buffer *b, View *v, MEVENT *ev)
{
	Line *line;
	int text_off;
	int map_w;
	int view_y;
	size_t file_row;

	if(b == NULL || v == NULL || ev == NULL)
	{
		return 0;
	}

	if(ev->y < 0 || ev->y >= LINES - 2)
	{
		return 0;
	}

	if(ev->y == 0)
	{
		int tab;

		tab = tabs_hit_tab(ev->x);

		if(tab < 0)
		{
			return 0;
		}

		tabs_activate(tab);
		return 1;
	}

	if(b->num_lines == 0)
	{
		return 0;
	}

	view_y = ev->y - VIEW_TOP;
	map_w = editor_map_width(b);

	if(map_w > 0 && ev->x >= COLS - map_w)
	{
		size_t target;

		target = editor_map_bucket_start(b, view_y);

		if(target >= b->num_lines)
		{
			return 0;
		}

		v->cy = target;
		editor_select_clear(v);
		complete_reset(b);
		search_deactivate();
		editor_clamp_cursor(b, v);
		v->want_cx = v->cx;
		return 1;
	}

	file_row = v->row_off + (size_t)view_y;

	if(file_row >= b->num_lines)
	{
		return 0;
	}

	text_off = editor_text_offset(b);
	v->cy = file_row;
	line = &b->lines[file_row];

	if(ev->x <= text_off)
	{
		v->cx = 0;
	}
	else
	{
		v->cx = line_byte_at_cell(line, v->col_off, ev->x - text_off);
	}

	editor_clamp_cursor(b, v);
	v->want_cx = v->cx;
	editor_select_clear(v);
	complete_reset(b);
	search_deactivate();
	return 1;
}

int mouse_handle(Buffer *b, View *v)
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
		mouse_scroll(b, v, -1);
		return 1;
	}

	if((ev.bstate & BUTTON5_PRESSED) != 0)
	{
		mouse_scroll(b, v, 1);
		return 1;
	}

	if((ev.bstate & BUTTON1_PRESSED) != 0)
	{
		return mouse_click(b, v, &ev);
	}

	if((ev.bstate & BUTTON1_CLICKED) != 0)
	{
		return mouse_click(b, v, &ev);
	}

	if((ev.bstate & BUTTON1_DOUBLE_CLICKED) != 0)
	{
		return mouse_click(b, v, &ev);
	}

	return 0;
}
