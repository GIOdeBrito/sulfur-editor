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
static int s_dragging;
static size_t s_ax;
static size_t s_ay;

void mouse_init(void)
{
	mmask_t result;

	s_enabled = 0;
	s_dragging = 0;
	s_ax = 0;
	s_ay = 0;
	result = mousemask((mmask_t)(BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED | BUTTON4_PRESSED | BUTTON5_PRESSED | REPORT_MOUSE_POSITION), NULL);

	if(result == 0)
	{
		return;
	}

	mouseinterval(0);
	s_enabled = 1;
}

void mouse_cancel_drag(void)
{
	s_dragging = 0;
}

static void mouse_scroll(Buffer *b, View *v, int direction)
{
	size_t max_cy;

	if(b == NULL || v == NULL)
	{
		return;
	}

	s_dragging = 0;
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

static int mouse_column_for_row(Buffer *b, View *v, int x, size_t file_row, size_t *out_cx)
{
	Line *line;
	int text_off;
	int map_w;

	if(b == NULL || v == NULL || out_cx == NULL)
	{
		return 0;
	}

	if(file_row >= b->num_lines)
	{
		*out_cx = 0;
		return 1;
	}

	text_off = editor_text_offset(b);

	if(x <= text_off)
	{
		*out_cx = 0;
		return 1;
	}

	line = &b->lines[file_row];
	map_w = editor_map_width(b);

	if(map_w > 0 && x >= COLS - map_w)
	{
		*out_cx = line->len;
		return 1;
	}

	*out_cx = line_byte_at_cell(line, v->col_off, x - text_off);
	return 1;
}

static int mouse_press(Buffer *b, View *v, MEVENT *ev)
{
	int map_w;
	int view_y;
	size_t file_row;
	size_t cx;

	if(b == NULL || v == NULL || ev == NULL)
	{
		return 0;
	}

	if(ev->y < 0 || ev->y >= LINES - 2)
	{
		s_dragging = 0;
		return 0;
	}

	if(ev->y == 0)
	{
		int tab;

		tab = tabs_hit_tab(ev->x);

		if(tab < 0)
		{
			s_dragging = 0;
			return 0;
		}

		s_dragging = 0;
		tabs_activate(tab);
		return 1;
	}

	if(b->num_lines == 0)
	{
		s_dragging = 0;
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
			s_dragging = 0;
			return 0;
		}

		s_dragging = 0;
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
		s_dragging = 0;
		return 0;
	}

	if(mouse_column_for_row(b, v, ev->x, file_row, &cx) == 0)
	{
		s_dragging = 0;
		return 0;
	}

	v->cy = file_row;
	v->cx = cx;
	editor_clamp_cursor(b, v);
	v->want_cx = v->cx;
	editor_select_clear(v);
	complete_reset(b);
	search_deactivate();
	s_ax = v->cx;
	s_ay = v->cy;
	s_dragging = 1;
	return 1;
}

static void mouse_extend_to(Buffer *b, View *v, size_t file_row, size_t cx)
{
	size_t max_cy;

	if(b == NULL || v == NULL)
	{
		return;
	}

	max_cy = editor_max_cy(b);

	if(file_row > max_cy)
	{
		file_row = max_cy;
	}

	if(file_row >= b->num_lines)
	{
		cx = 0;
	}

	v->sel_ax = s_ax;
	v->sel_ay = s_ay;
	v->cy = file_row;
	v->cx = cx;
	editor_clamp_cursor(b, v);

	if(v->sel_ay == v->cy && v->sel_ax == v->cx)
	{
		v->sel_active = 0;
	}
	else
	{
		v->sel_active = 1;
	}

	editor_scroll(b, v);

	if(v->cy < b->num_lines)
	{
		v->want_cx = v->cx;
	}
}

static int mouse_motion(Buffer *b, View *v, MEVENT *ev)
{
	int view_rows;
	size_t file_row;
	size_t cx;

	if(b == NULL || v == NULL || ev == NULL)
	{
		return 0;
	}

	if(s_dragging == 0)
	{
		return 0;
	}

	if(b->num_lines == 0)
	{
		s_dragging = 0;
		return 0;
	}

	view_rows = editor_view_rows();

	if(view_rows < 1)
	{
		view_rows = 1;
	}

	if(ev->y < VIEW_TOP)
	{
		if(v->cy == 0)
		{
			return 1;
		}

		file_row = v->cy - 1;

		if(mouse_column_for_row(b, v, ev->x, file_row, &cx) == 0)
		{
			return 1;
		}

		mouse_extend_to(b, v, file_row, cx);
		return 1;
	}

	if(ev->y >= VIEW_TOP + view_rows)
	{
		size_t max_cy;

		max_cy = editor_max_cy(b);

		if(v->cy >= max_cy)
		{
			return 1;
		}

		file_row = v->cy + 1;

		if(mouse_column_for_row(b, v, ev->x, file_row, &cx) == 0)
		{
			return 1;
		}

		mouse_extend_to(b, v, file_row, cx);
		return 1;
	}

	file_row = v->row_off + (size_t)(ev->y - VIEW_TOP);

	if(file_row > editor_max_cy(b))
	{
		file_row = editor_max_cy(b);
	}

	if(mouse_column_for_row(b, v, ev->x, file_row, &cx) == 0)
	{
		return 1;
	}

	mouse_extend_to(b, v, file_row, cx);
	return 1;
}

static int mouse_release(Buffer *b, View *v, MEVENT *ev)
{
	int view_rows;
	int y;
	size_t file_row;
	size_t cx;

	if(b == NULL || v == NULL || ev == NULL)
	{
		return 0;
	}

	if(s_dragging == 0)
	{
		return 0;
	}

	s_dragging = 0;

	if(b->num_lines == 0)
	{
		return 1;
	}

	view_rows = editor_view_rows();

	if(view_rows < 1)
	{
		view_rows = 1;
	}

	y = ev->y;

	if(y < VIEW_TOP)
	{
		y = VIEW_TOP;
	}

	if(y >= VIEW_TOP + view_rows)
	{
		y = VIEW_TOP + view_rows - 1;
	}

	if(y < 0 || y >= LINES - 2 || y == 0)
	{
		if(v->sel_ay == v->cy && v->sel_ax == v->cx)
		{
			editor_select_clear(v);
		}

		return 1;
	}

	file_row = v->row_off + (size_t)(y - VIEW_TOP);

	if(file_row > editor_max_cy(b))
	{
		file_row = editor_max_cy(b);
	}

	if(mouse_column_for_row(b, v, ev->x, file_row, &cx) == 0)
	{
		return 1;
	}

	mouse_extend_to(b, v, file_row, cx);
	return 1;
}

static int mouse_click(Buffer *b, View *v, MEVENT *ev)
{
	int result;

	result = mouse_press(b, v, ev);
	s_dragging = 0;
	return result;
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

	if((ev.bstate & REPORT_MOUSE_POSITION) != 0)
	{
		if(s_dragging != 0 && (ev.bstate & BUTTON1_PRESSED) != 0)
		{
			return mouse_motion(b, v, &ev);
		}

		return 0;
	}

	if((ev.bstate & BUTTON1_RELEASED) != 0)
	{
		return mouse_release(b, v, &ev);
	}

	if((ev.bstate & BUTTON1_PRESSED) != 0)
	{
		if(s_dragging != 0)
		{
			return mouse_motion(b, v, &ev);
		}

		return mouse_press(b, v, &ev);
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
