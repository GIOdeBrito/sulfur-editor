#include <ncurses.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "complete.h"
#include "editor.h"
#include "line.h"
#include "search.h"

#define SEARCH_MAX_QUERY 128

static char s_query[SEARCH_MAX_QUERY];
static int s_active;

void search_reset(void)
{
	s_query[0] = '\0';
	s_active = 0;
}

void search_deactivate(void)
{
	s_active = 0;
}

int search_is_active(void)
{
	return s_active;
}

static void search_count_text(Buffer *b, char *buf, size_t cap)
{
	if(b == NULL || buf == NULL)
	{
		return;
	}

	if(b->search.truncated != 0)
	{
		snprintf(buf, cap, "%zu/%zu+ (first %d)", b->search.current + 1, b->search.count, SEARCH_MAX_MATCHES);
		return;
	}

	snprintf(buf, cap, "%zu/%zu", b->search.current + 1, b->search.count);
}

static void search_goto_current(Buffer *b, View *v)
{
	char count[64];
	char msg[STATUS_LIMIT];

	if(b == NULL || v == NULL)
	{
		return;
	}

	if(b->search.count == 0 || b->search.current >= b->search.count)
	{
		return;
	}

	v->cy = b->search.matches[b->search.current].row;
	v->cx = b->search.matches[b->search.current].col;
	v->want_cx = v->cx;
	editor_select_clear(v);
	complete_reset(b);
	editor_clamp_cursor(b, v);
	search_count_text(b, count, sizeof(count));
	snprintf(msg, sizeof(msg), "Match %s: %s", count, s_query);
	editor_set_status(msg);
}

static void search_rescan(Buffer *b)
{
	size_t qlen;
	size_t row;

	if(b == NULL)
	{
		return;
	}

	b->search.count = 0;
	b->search.current = 0;
	b->search.truncated = 0;
	qlen = strlen(s_query);

	if(qlen == 0)
	{
		return;
	}

	for(row = 0; row < b->num_lines; row++)
	{
		Line *line;
		const char *p;

		line = &b->lines[row];

		if(line->data == NULL || line->len < qlen)
		{
			continue;
		}

		p = line->data;

		while((p = strstr(p, s_query)) != NULL)
		{
			if(b->search.count >= SEARCH_MAX_MATCHES)
			{
				b->search.truncated = 1;
				return;
			}

			b->search.matches[b->search.count].row = row;
			b->search.matches[b->search.count].col = (size_t)(p - line->data);
			b->search.count++;
			p += qlen;
		}
	}
}

static void search_jump_from_cursor(Buffer *b, View *v)
{
	size_t i;

	if(b == NULL || v == NULL)
	{
		return;
	}

	for(i = 0; i < b->search.count; i++)
	{
		if(b->search.matches[i].row > v->cy)
		{
			break;
		}

		if(b->search.matches[i].row == v->cy && b->search.matches[i].col >= v->cx)
		{
			break;
		}
	}

	if(i >= b->search.count)
	{
		i = 0;
	}

	b->search.current = i;
	s_active = 1;
	search_goto_current(b, v);
}

void search_prompt_and_find(Buffer *b, View *v)
{
	char query[SEARCH_MAX_QUERY];
	char msg[STATUS_LIMIT];
	int rows;

	if(b == NULL || v == NULL)
	{
		return;
	}

	complete_reset(b);
	rows = LINES - 1;
	echo();
	curs_set(1);
	mvprintw(rows, 0, "Find: ");
	clrtoeol();
	query[0] = '\0';

	if(getnstr(query, (int)sizeof(query) - 1) != OK)
	{
		noecho();
		search_deactivate();
		editor_set_status("Search cancelled");
		return;
	}

	noecho();

	if(query[0] == '\0')
	{
		if(s_query[0] == '\0')
		{
			search_deactivate();
			editor_set_status("Search cancelled");
			return;
		}

		search_rescan(b);

		if(b->search.count == 0)
		{
			s_active = 0;
			snprintf(msg, sizeof(msg), "Not found: %s", s_query);
			editor_set_status(msg);
			return;
		}

		search_jump_from_cursor(b, v);
		return;
	}

	snprintf(s_query, sizeof(s_query), "%s", query);
	search_rescan(b);

	if(b->search.count == 0)
	{
		s_active = 0;
		snprintf(msg, sizeof(msg), "Not found: %s", s_query);
		editor_set_status(msg);
		return;
	}

	search_jump_from_cursor(b, v);
}

void search_next(Buffer *b, View *v, int direction)
{
	if(b == NULL || v == NULL)
	{
		return;
	}

	if(s_active == 0 || b->search.count == 0)
	{
		return;
	}

	if(direction < 0)
	{
		b->search.current = (b->search.current + b->search.count - 1) % b->search.count;
	}
	else
	{
		b->search.current = (b->search.current + 1) % b->search.count;
	}

	search_goto_current(b, v);
}

static int search_is_nav_key(int key)
{
	if(key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN)
	{
		return 1;
	}

	if(key == KEY_SLEFT || key == KEY_SRIGHT || key == KEY_SR || key == KEY_SF)
	{
		return 1;
	}

	if(key == KEY_PPAGE || key == KEY_NPAGE || key == KEY_SPREVIOUS || key == KEY_SNEXT)
	{
		return 1;
	}

	if(key == KEY_HOME || key == KEY_END || key == KEY_SHOME || key == KEY_SEND)
	{
		return 1;
	}

	if(key == KEY_MOUSE || key == KEY_RESIZE)
	{
		return 1;
	}

	return 0;
}

int search_handle_key(Buffer *b, View *v, int key)
{
	if(s_active == 0)
	{
		return 0;
	}

	if(b == NULL || v == NULL)
	{
		return 0;
	}

	if(key == 'n' || key == KEY_ENTER || key == '\n' || key == '\r')
	{
		search_next(b, v, 1);
		return 1;
	}

	if(key == 'N')
	{
		search_next(b, v, -1);
		return 1;
	}

	if(key == 27)
	{
		search_deactivate();
		editor_set_status("Search cancelled");
		return 1;
	}

	if(key == CTRL_KEY('f'))
	{
		search_prompt_and_find(b, v);
		return 1;
	}

	if(search_is_nav_key(key) != 0)
	{
		return 0;
	}

	search_deactivate();
	return 0;
}

int search_cover(Buffer *b, size_t row, size_t col, int *is_current)
{
	size_t qlen;
	size_t lo;
	size_t hi;

	if(is_current != NULL)
	{
		*is_current = 0;
	}

	if(b == NULL)
	{
		return 0;
	}

	if(s_active == 0 || b->search.count == 0)
	{
		return 0;
	}

	qlen = strlen(s_query);

	if(qlen == 0)
	{
		return 0;
	}

	lo = 0;
	hi = b->search.count;

	while(lo < hi)
	{
		size_t mid;

		mid = lo + (hi - lo) / 2;

		if(b->search.matches[mid].row < row || (b->search.matches[mid].row == row && b->search.matches[mid].col <= col))
		{
			lo = mid + 1;
		}
		else
		{
			hi = mid;
		}
	}

	if(lo == 0)
	{
		return 0;
	}

	lo--;

	if(b->search.matches[lo].row != row)
	{
		return 0;
	}

	if(col - b->search.matches[lo].col >= qlen)
	{
		return 0;
	}

	if(is_current != NULL && lo == b->search.current)
	{
		*is_current = 1;
	}

	return 1;
}
