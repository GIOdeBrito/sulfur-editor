#include <ncurses.h>
#include <string.h>

#include "common.h"
#include "complete.h"
#include "edit.h"
#include "editor.h"
#include "git.h"
#include "highlight.h"
#include "line.h"
#include "search.h"
#include "undo.h"
#include "view.h"

#define COMPLETE_MAX_VISIBLE 8

static int is_word_char(int c)
{
	if(c >= 'a' && c <= 'z')
	{
		return 1;
	}

	if(c >= 'A' && c <= 'Z')
	{
		return 1;
	}

	if(c >= '0' && c <= '9')
	{
		return 1;
	}

	return c == '_';
}

static size_t prefix_start_of(Line *line, size_t cx)
{
	size_t start;

	start = cx;

	while(start > 0 && is_word_char((unsigned char)line->data[start - 1]))
	{
		start--;
	}

	return start;
}

static void consider_word(Buffer *b, const char *word, size_t len, size_t prefix_len)
{
	CompleteState *c;
	int i;

	if(b == NULL)
	{
		return;
	}

	c = &b->complete;

	if(len <= prefix_len)
	{
		return;
	}

	if(len >= COMPLETE_MAX_WORD)
	{
		return;
	}

	if(memcmp(word, c->prefix, prefix_len) != 0)
	{
		return;
	}

	for(i = 0; i < c->count; i++)
	{
		if(strlen(c->items[i]) == len && memcmp(c->items[i], word, len) == 0)
		{
			c->freq[i]++;
			return;
		}
	}

	if(c->count >= COMPLETE_MAX_ITEMS)
	{
		return;
	}

	memcpy(c->items[c->count], word, len);
	c->items[c->count][len] = '\0';
	c->freq[c->count] = 1;
	c->count++;
}

static void collect_candidates(Buffer *b, size_t prefix_len)
{
	size_t li;

	if(b == NULL)
	{
		return;
	}

	for(li = 0; li < b->num_lines; li++)
	{
		Line *line;
		size_t i;

		line = &b->lines[li];

		if(line->data == NULL)
		{
			continue;
		}

		i = 0;

		while(i < line->len)
		{
			size_t j;

			if(is_word_char((unsigned char)line->data[i]) == 0)
			{
				i++;
				continue;
			}

			j = i;

			while(j < line->len && is_word_char((unsigned char)line->data[j]))
			{
				j++;
			}

			consider_word(b, &line->data[i], j - i, prefix_len);
			i = j;
		}
	}
}

static void sort_candidates(Buffer *b)
{
	CompleteState *c;
	int i;

	if(b == NULL)
	{
		return;
	}

	c = &b->complete;

	for(i = 1; i < c->count; i++)
	{
		char word[COMPLETE_MAX_WORD];
		int freq;
		int j;

		memcpy(word, c->items[i], sizeof(word));
		freq = c->freq[i];
		j = i - 1;

		while(j >= 0 && (c->freq[j] < freq || (c->freq[j] == freq && strcmp(c->items[j], word) > 0)))
		{
			memcpy(c->items[j + 1], c->items[j], sizeof(word));
			c->freq[j + 1] = c->freq[j];
			j--;
		}

		memcpy(c->items[j + 1], word, sizeof(word));
		c->freq[j + 1] = freq;
	}
}

static void rebuild(Buffer *b, View *v, size_t min_len)
{
	CompleteState *c;
	Line *line;
	size_t start;
	size_t len;

	if(b == NULL || v == NULL)
	{
		return;
	}

	complete_reset(b);
	c = &b->complete;
	line = editor_current_line(b, v);

	if(line == NULL || line->data == NULL)
	{
		return;
	}

	if(v->cx > line->len)
	{
		return;
	}

	start = prefix_start_of(line, v->cx);
	len = v->cx - start;

	if(len < min_len)
	{
		return;
	}

	if(len >= COMPLETE_MAX_WORD)
	{
		start = v->cx - (COMPLETE_MAX_WORD - 1);
		len = COMPLETE_MAX_WORD - 1;
	}

	memcpy(c->prefix, &line->data[start], len);
	c->prefix[len] = '\0';
	c->prefix_start = start;
	c->prefix_line = v->cy;
	c->count = 0;
	c->selected = 0;
	collect_candidates(b, len);

	if(c->count == 0)
	{
		complete_reset(b);
		return;
	}

	sort_candidates(b);
	c->active = 1;
}

void complete_reset(Buffer *b)
{
	if(b == NULL)
	{
		return;
	}

	b->complete.active = 0;
	b->complete.count = 0;
	b->complete.selected = 0;
	b->complete.prefix[0] = '\0';
	b->complete.prefix_start = 0;
	b->complete.prefix_line = 0;
}

int complete_is_active(Buffer *b)
{
	if(b == NULL)
	{
		return 0;
	}

	return b->complete.active;
}

void complete_update(Buffer *b, View *v)
{
	rebuild(b, v, COMPLETE_MIN_PREFIX);
}

void complete_update_forced(Buffer *b, View *v)
{
	rebuild(b, v, 1);
}

void complete_move(Buffer *b, int delta)
{
	CompleteState *c;

	if(b == NULL)
	{
		return;
	}

	c = &b->complete;

	if(c->active == 0 || c->count == 0)
	{
		return;
	}

	c->selected = (c->selected + delta) % c->count;

	if(c->selected < 0)
	{
		c->selected += c->count;
	}
}

void complete_accept(Buffer *b, View *v)
{
	CompleteState *c;
	Line *line;
	char candidate[COMPLETE_MAX_WORD];
	size_t candidate_len;
	size_t prefix_len;

	if(b == NULL || v == NULL)
	{
		return;
	}

	c = &b->complete;

	if(c->active == 0 || c->count == 0)
	{
		return;
	}

	if(c->selected < 0 || c->selected >= c->count)
	{
		c->selected = 0;
	}

	editor_select_clear(v);
	search_deactivate();
	line = editor_current_line(b, v);

	if(line == NULL || line->data == NULL)
	{
		complete_reset(b);
		return;
	}

	if(c->prefix_line != v->cy)
	{
		complete_reset(b);
		return;
	}

	if(c->prefix_start > v->cx || v->cx > line->len)
	{
		complete_reset(b);
		return;
	}

	memcpy(candidate, c->items[c->selected], sizeof(candidate));
	candidate_len = strlen(candidate);
	prefix_len = v->cx - c->prefix_start;
	undo_begin(b, v, UNDO_OTHER);
	line_ensure_cap(line, line->len - prefix_len + candidate_len + 1);
	memmove(&line->data[c->prefix_start + candidate_len], &line->data[v->cx], line->len - v->cx + 1);
	memcpy(&line->data[c->prefix_start], candidate, candidate_len);
	line->len = line->len - prefix_len + candidate_len;
	v->cx = c->prefix_start + candidate_len;

	if(v->cx >= line->len || line->data[v->cx] != ' ')
	{
		line_insert_char(line, v->cx, ' ');
		v->cx++;
	}

	v->want_cx = v->cx;
	b->dirty = 1;
	highlight_mark_dirty(b);
	git_touch(b);
	undo_end(b, v, UNDO_OTHER);
	complete_reset(b);
}

int complete_handle_key(Buffer *b, View *v, int key)
{
	if(b == NULL || v == NULL)
	{
		return 0;
	}

	if(key == KEY_UP || key == CTRL_KEY('p'))
	{
		complete_move(b, -1);
		return 1;
	}

	if(key == KEY_DOWN || key == CTRL_KEY('n'))
	{
		complete_move(b, 1);
		return 1;
	}

	if(key == '\t' || key == KEY_ENTER || key == '\n' || key == '\r')
	{
		complete_accept(b, v);
		return 1;
	}

	if(key == 27)
	{
		complete_reset(b);
		editor_set_status("Completion cancelled");
		return 1;
	}

	if(key == KEY_BACKSPACE || key == 127 || key == CTRL_KEY('h'))
	{
		editor_delete_backspace(b, v);
		complete_update(b, v);
		return 1;
	}

	if(key >= 32 && key <= 126 && is_word_char(key))
	{
		editor_insert_char(b, v, (char)key);
		complete_update(b, v);
		return 1;
	}

	return 0;
}

void complete_draw(Buffer *b, View *v)
{
	CompleteState *c;
	Line *line;
	int text_x;
	int cursor_y;
	int x;
	int y;
	int width;
	int height;
	int visible;
	int start;
	int i;
	int r;

	if(b == NULL || v == NULL)
	{
		return;
	}

	c = &b->complete;

	if(c->active == 0 || c->count == 0)
	{
		return;
	}

	if(c->prefix_line != v->cy)
	{
		complete_reset(b);
		return;
	}

	if(LINES < 5 || COLS < 10)
	{
		return;
	}

	visible = c->count;

	if(visible > COMPLETE_MAX_VISIBLE)
	{
		visible = COMPLETE_MAX_VISIBLE;
	}

	start = 0;

	if(c->selected >= visible)
	{
		start = c->selected - visible + 1;
	}

	line = editor_current_line(b, v);

	if(line == NULL)
	{
		return;
	}

	width = 0;

	for(i = 0; i < visible; i++)
	{
		int w;

		w = utf8_width_n(c->items[start + i], strlen(c->items[start + i]));

		if(w > width)
		{
			width = w;
		}
	}

	width += 2;

	if(width + 2 > COLS)
	{
		return;
	}

	text_x = editor_text_offset(b);
	cursor_y = (int)v->cy - (int)v->row_off + VIEW_TOP;
	x = text_x + (int)line_cells_upto(line, c->prefix_start) -
		(int)line_cells_upto(line, v->col_off);

	if(x < text_x)
	{
		x = text_x;
	}

	if(x + width + 2 > COLS)
	{
		x = COLS - width - 2;
	}

	if(x < 0)
	{
		return;
	}

	height = visible + 2;
	y = cursor_y + 1;

	if(y + height > LINES - 2)
	{
		y = cursor_y - height;
	}

	if(y < VIEW_TOP)
	{
		return;
	}

	mvaddch(y, x, ACS_ULCORNER);

	for(i = 1; i <= width; i++)
	{
		mvaddch(y, x + i, ACS_HLINE);
	}

	mvaddch(y, x + width + 1, ACS_URCORNER);

	for(r = 0; r < visible; r++)
	{
		int selected;

		mvaddch(y + 1 + r, x, ACS_VLINE);

		for(i = 1; i <= width; i++)
		{
			mvaddch(y + 1 + r, x + i, ' ');
		}

		mvaddch(y + 1 + r, x + width + 1, ACS_VLINE);
		selected = (start + r == c->selected);

		if(selected)
		{
			attron(A_REVERSE);
		}

		mvaddnstr(y + 1 + r, x + 1, c->items[start + r],
			(int)utf8_fit_cells(c->items[start + r],
				strlen(c->items[start + r]), width));

		if(selected)
		{
			attroff(A_REVERSE);
		}
	}

	mvaddch(y + height - 1, x, ACS_LLCORNER);

	for(i = 1; i <= width; i++)
	{
		mvaddch(y + height - 1, x + i, ACS_HLINE);
	}

	mvaddch(y + height - 1, x + width + 1, ACS_LRCORNER);
}
