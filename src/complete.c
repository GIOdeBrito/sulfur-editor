#include <ncurses.h>
#include <string.h>

#include "common.h"
#include "complete.h"
#include "edit.h"
#include "editor.h"
#include "line.h"
#include "view.h"

#define COMPLETE_MIN_PREFIX 2
#define COMPLETE_MAX_WORD 64
#define COMPLETE_MAX_ITEMS 32
#define COMPLETE_MAX_VISIBLE 8

typedef struct
{
	int active;
	char prefix[COMPLETE_MAX_WORD];
	char items[COMPLETE_MAX_ITEMS][COMPLETE_MAX_WORD];
	int freq[COMPLETE_MAX_ITEMS];
	int count;
	int selected;
	size_t prefix_start;
	size_t prefix_line;
} Complete;

static Complete s_complete;

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

static void consider_word(const char *word, size_t len, size_t prefix_len)
{
	int i;

	if(len <= prefix_len)
	{
		return;
	}

	if(len >= COMPLETE_MAX_WORD)
	{
		return;
	}

	if(memcmp(word, s_complete.prefix, prefix_len) != 0)
	{
		return;
	}

	for(i = 0; i < s_complete.count; i++)
	{
		if(strlen(s_complete.items[i]) == len && memcmp(s_complete.items[i], word, len) == 0)
		{
			s_complete.freq[i]++;
			return;
		}
	}

	if(s_complete.count >= COMPLETE_MAX_ITEMS)
	{
		return;
	}

	memcpy(s_complete.items[s_complete.count], word, len);
	s_complete.items[s_complete.count][len] = '\0';
	s_complete.freq[s_complete.count] = 1;
	s_complete.count++;
}

static void collect_candidates(size_t prefix_len)
{
	size_t li;

	for(li = 0; li < g_editor.num_lines; li++)
	{
		Line *line;
		size_t i;

		line = &g_editor.lines[li];

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

			consider_word(&line->data[i], j - i, prefix_len);
			i = j;
		}
	}
}

static void sort_candidates(void)
{
	int i;

	for(i = 1; i < s_complete.count; i++)
	{
		char word[COMPLETE_MAX_WORD];
		int freq;
		int j;

		memcpy(word, s_complete.items[i], sizeof(word));
		freq = s_complete.freq[i];
		j = i - 1;

		while(j >= 0 && (s_complete.freq[j] < freq || (s_complete.freq[j] == freq && strcmp(s_complete.items[j], word) > 0)))
		{
			memcpy(s_complete.items[j + 1], s_complete.items[j], sizeof(word));
			s_complete.freq[j + 1] = s_complete.freq[j];
			j--;
		}

		memcpy(s_complete.items[j + 1], word, sizeof(word));
		s_complete.freq[j + 1] = freq;
	}
}

static void rebuild(size_t min_len)
{
	Line *line;
	size_t start;
	size_t len;

	complete_reset();
	line = editor_current_line();

	if(line == NULL || line->data == NULL)
	{
		return;
	}

	if(g_editor.cx > line->len)
	{
		return;
	}

	start = prefix_start_of(line, g_editor.cx);
	len = g_editor.cx - start;

	if(len < min_len)
	{
		return;
	}

	if(len >= COMPLETE_MAX_WORD)
	{
		start = g_editor.cx - (COMPLETE_MAX_WORD - 1);
		len = COMPLETE_MAX_WORD - 1;
	}

	memcpy(s_complete.prefix, &line->data[start], len);
	s_complete.prefix[len] = '\0';
	s_complete.prefix_start = start;
	s_complete.prefix_line = g_editor.cy;
	s_complete.count = 0;
	s_complete.selected = 0;
	collect_candidates(len);

	if(s_complete.count == 0)
	{
		complete_reset();
		return;
	}

	sort_candidates();
	s_complete.active = 1;
}

void complete_reset(void)
{
	s_complete.active = 0;
	s_complete.count = 0;
	s_complete.selected = 0;
	s_complete.prefix[0] = '\0';
	s_complete.prefix_start = 0;
	s_complete.prefix_line = 0;
}

int complete_is_active(void)
{
	return s_complete.active;
}

void complete_update(void)
{
	rebuild(COMPLETE_MIN_PREFIX);
}

void complete_update_forced(void)
{
	rebuild(1);
}

void complete_move(int delta)
{
	if(s_complete.active == 0 || s_complete.count == 0)
	{
		return;
	}

	s_complete.selected = (s_complete.selected + delta) % s_complete.count;

	if(s_complete.selected < 0)
	{
		s_complete.selected += s_complete.count;
	}
}

void complete_accept(void)
{
	Line *line;
	char candidate[COMPLETE_MAX_WORD];
	size_t candidate_len;
	size_t prefix_len;

	if(s_complete.active == 0 || s_complete.count == 0)
	{
		return;
	}

	if(s_complete.selected < 0 || s_complete.selected >= s_complete.count)
	{
		s_complete.selected = 0;
	}

	line = editor_current_line();

	if(line == NULL || line->data == NULL)
	{
		complete_reset();
		return;
	}

	if(s_complete.prefix_line != g_editor.cy)
	{
		complete_reset();
		return;
	}

	if(s_complete.prefix_start > g_editor.cx || g_editor.cx > line->len)
	{
		complete_reset();
		return;
	}

	memcpy(candidate, s_complete.items[s_complete.selected], sizeof(candidate));
	candidate_len = strlen(candidate);
	prefix_len = g_editor.cx - s_complete.prefix_start;
	line_ensure_cap(line, line->len - prefix_len + candidate_len + 1);
	memmove(&line->data[s_complete.prefix_start + candidate_len], &line->data[g_editor.cx], line->len - g_editor.cx + 1);
	memcpy(&line->data[s_complete.prefix_start], candidate, candidate_len);
	line->len = line->len - prefix_len + candidate_len;
	g_editor.cx = s_complete.prefix_start + candidate_len;
	g_editor.want_cx = g_editor.cx;
	g_editor.dirty = 1;
	complete_reset();
}

int complete_handle_key(int key)
{
	if(key == KEY_UP || key == CTRL_KEY('p'))
	{
		complete_move(-1);
		return 1;
	}

	if(key == KEY_DOWN || key == CTRL_KEY('n'))
	{
		complete_move(1);
		return 1;
	}

	if(key == '\t' || key == KEY_ENTER || key == '\n' || key == '\r')
	{
		complete_accept();
		return 1;
	}

	if(key == 27)
	{
		complete_reset();
		editor_set_status("Completion cancelled");
		return 1;
	}

	if(key == KEY_BACKSPACE || key == 127 || key == CTRL_KEY('h'))
	{
		editor_delete_backspace();
		complete_update();
		return 1;
	}

	if(key >= 32 && key <= 126 && is_word_char(key))
	{
		editor_insert_char((char)key);
		complete_update();
		return 1;
	}

	return 0;
}

void complete_draw(void)
{
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

	if(s_complete.active == 0 || s_complete.count == 0)
	{
		return;
	}

	if(s_complete.prefix_line != g_editor.cy)
	{
		complete_reset();
		return;
	}

	if(LINES < 5 || COLS < 10)
	{
		return;
	}

	visible = s_complete.count;

	if(visible > COMPLETE_MAX_VISIBLE)
	{
		visible = COMPLETE_MAX_VISIBLE;
	}

	start = 0;

	if(s_complete.selected >= visible)
	{
		start = s_complete.selected - visible + 1;
	}

	width = 0;

	for(i = 0; i < visible; i++)
	{
		int w;

		w = (int)strlen(s_complete.items[start + i]);

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

	text_x = editor_text_offset();
	cursor_y = (int)g_editor.cy - (int)g_editor.row_off;
	x = text_x + (int)s_complete.prefix_start - (int)g_editor.col_off;

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

	if(y < 0)
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
		selected = (start + r == s_complete.selected);

		if(selected)
		{
			attron(A_REVERSE);
		}

		mvaddnstr(y + 1 + r, x + 1, s_complete.items[start + r], width);

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
