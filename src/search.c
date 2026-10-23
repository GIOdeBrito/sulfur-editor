#include <ncurses.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "complete.h"
#include "editor.h"
#include "line.h"
#include "search.h"

#define SEARCH_MAX_QUERY 128
#define SEARCH_MAX_MATCHES 512

typedef struct
{
	size_t row;
	size_t col;
} SearchMatch;

static char s_query[SEARCH_MAX_QUERY];
static SearchMatch s_matches[SEARCH_MAX_MATCHES];
static size_t s_count;
static size_t s_current;
static int s_active;
static int s_truncated;

void search_reset(void)
{
	s_query[0] = '\0';
	s_count = 0;
	s_current = 0;
	s_active = 0;
	s_truncated = 0;
}

void search_deactivate(void)
{
	s_active = 0;
}

int search_is_active(void)
{
	return s_active;
}

static void search_count_text(char *buf, size_t cap)
{
	if(buf == NULL)
	{
		return;
	}

	if(s_truncated != 0)
	{
		snprintf(buf, cap, "%zu/%zu+ (first %d)", s_current + 1, s_count, SEARCH_MAX_MATCHES);
		return;
	}

	snprintf(buf, cap, "%zu/%zu", s_current + 1, s_count);
}

static void search_goto_current(void)
{
	char count[64];
	char msg[STATUS_LIMIT];

	if(s_count == 0 || s_current >= s_count)
	{
		return;
	}

	g_editor.cy = s_matches[s_current].row;
	g_editor.cx = s_matches[s_current].col;
	g_editor.want_cx = g_editor.cx;
	editor_select_clear();
	complete_reset();
	editor_clamp_cursor();
	search_count_text(count, sizeof(count));
	snprintf(msg, sizeof(msg), "Match %s: %s", count, s_query);
	editor_set_status(msg);
}

static void search_rescan(void)
{
	size_t qlen;
	size_t row;

	s_count = 0;
	s_current = 0;
	s_truncated = 0;
	qlen = strlen(s_query);

	if(qlen == 0)
	{
		return;
	}

	for(row = 0; row < g_editor.num_lines; row++)
	{
		Line *line;
		const char *p;

		line = &g_editor.lines[row];

		if(line->data == NULL || line->len < qlen)
		{
			continue;
		}

		p = line->data;

		while((p = strstr(p, s_query)) != NULL)
		{
			if(s_count >= SEARCH_MAX_MATCHES)
			{
				s_truncated = 1;
				return;
			}

			s_matches[s_count].row = row;
			s_matches[s_count].col = (size_t)(p - line->data);
			s_count++;
			p += qlen;
		}
	}
}

static void search_jump_from_cursor(void)
{
	size_t i;

	for(i = 0; i < s_count; i++)
	{
		if(s_matches[i].row > g_editor.cy)
		{
			break;
		}

		if(s_matches[i].row == g_editor.cy && s_matches[i].col >= g_editor.cx)
		{
			break;
		}
	}

	if(i >= s_count)
	{
		i = 0;
	}

	s_current = i;
	s_active = 1;
	search_goto_current();
}

void search_prompt_and_find(void)
{
	char query[SEARCH_MAX_QUERY];
	char msg[STATUS_LIMIT];
	int rows;

	complete_reset();
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

		search_rescan();

		if(s_count == 0)
		{
			s_active = 0;
			snprintf(msg, sizeof(msg), "Not found: %s", s_query);
			editor_set_status(msg);
			return;
		}

		search_jump_from_cursor();
		return;
	}

	snprintf(s_query, sizeof(s_query), "%s", query);
	search_rescan();

	if(s_count == 0)
	{
		s_active = 0;
		snprintf(msg, sizeof(msg), "Not found: %s", s_query);
		editor_set_status(msg);
		return;
	}

	search_jump_from_cursor();
}

void search_next(int direction)
{
	if(s_active == 0 || s_count == 0)
	{
		return;
	}

	if(direction < 0)
	{
		s_current = (s_current + s_count - 1) % s_count;
	}
	else
	{
		s_current = (s_current + 1) % s_count;
	}

	search_goto_current();
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

int search_handle_key(int key)
{
	if(s_active == 0)
	{
		return 0;
	}

	if(key == 'n' || key == KEY_ENTER || key == '\n' || key == '\r')
	{
		search_next(1);
		return 1;
	}

	if(key == 'N')
	{
		search_next(-1);
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
		search_prompt_and_find();
		return 1;
	}

	if(search_is_nav_key(key) != 0)
	{
		return 0;
	}

	search_deactivate();
	return 0;
}

int search_cover(size_t row, size_t col, int *is_current)
{
	size_t qlen;
	size_t lo;
	size_t hi;

	if(is_current != NULL)
	{
		*is_current = 0;
	}

	if(s_active == 0 || s_count == 0)
	{
		return 0;
	}

	qlen = strlen(s_query);

	if(qlen == 0)
	{
		return 0;
	}

	lo = 0;
	hi = s_count;

	while(lo < hi)
	{
		size_t mid;

		mid = lo + (hi - lo) / 2;

		if(s_matches[mid].row < row || (s_matches[mid].row == row && s_matches[mid].col <= col))
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

	if(s_matches[lo].row != row)
	{
		return 0;
	}

	if(col - s_matches[lo].col >= qlen)
	{
		return 0;
	}

	if(is_current != NULL && lo == s_current)
	{
		*is_current = 1;
	}

	return 1;
}
