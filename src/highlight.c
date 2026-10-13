#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "editor.h"
#include "highlight.h"
#include "line.h"
#include "util.h"

#define HL_PAIR_KEYWORD 1
#define HL_PAIR_STRING 2
#define HL_PAIR_COMMENT 3
#define HL_PAIR_NUMBER 4
#define HL_PAIR_PREPROC 5

static const char *s_keywords[] =
{
	"auto", "break", "case", "char", "const", "continue", "default", "do",
	"double", "else", "enum", "extern", "float", "for", "goto", "if",
	"inline", "int", "long", "register", "restrict", "return", "short",
	"signed", "sizeof", "static", "struct", "switch", "typedef", "union",
	"unsigned", "void", "volatile", "while",
	"_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic",
	"_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local",
	"NULL", "true", "false"
};

static unsigned char *s_state;
static size_t s_state_cap;
static unsigned char *s_scratch;
static size_t s_scratch_cap;
static int s_dirty;
static int s_color;

static int is_alpha_underscore(int c)
{
	if(c >= 'a' && c <= 'z')
	{
		return 1;
	}

	if(c >= 'A' && c <= 'Z')
	{
		return 1;
	}

	return c == '_';
}

static int is_word_continue(int c)
{
	if(is_alpha_underscore(c))
	{
		return 1;
	}

	return c >= '0' && c <= '9';
}

static int is_keyword(const char *word, size_t len)
{
	size_t i;

	for(i = 0; i < sizeof(s_keywords) / sizeof(s_keywords[0]); i++)
	{
		if(strlen(s_keywords[i]) == len && memcmp(s_keywords[i], word, len) == 0)
		{
			return 1;
		}
	}

	return 0;
}

static void grow_state(size_t needed)
{
	unsigned char *grown;

	if(needed <= s_state_cap)
	{
		return;
	}

	grown = realloc(s_state, needed);

	if(grown == NULL)
	{
		die("realloc");
	}

	s_state = grown;
	s_state_cap = needed;
}

static void grow_scratch(size_t needed)
{
	unsigned char *grown;

	if(needed <= s_scratch_cap)
	{
		return;
	}

	grown = realloc(s_scratch, needed);

	if(grown == NULL)
	{
		die("realloc");
	}

	s_scratch = grown;
	s_scratch_cap = needed;
}

static int scan_line(const char *data, size_t len, int in_comment, unsigned char *out)
{
	size_t first;
	size_t i;

	first = 0;

	while(first < len && (data[first] == ' ' || data[first] == '\t'))
	{
		first++;
	}

	i = 0;

	while(i < len)
	{
		size_t j;

		if(in_comment)
		{
			if(data[i] == '*' && i + 1 < len && data[i + 1] == '/')
			{
				out[i] = (unsigned char)HL_COMMENT;
				out[i + 1] = (unsigned char)HL_COMMENT;
				i += 2;
				in_comment = 0;
				continue;
			}

			out[i] = (unsigned char)HL_COMMENT;
			i++;
			continue;
		}

		if(data[i] == '/' && i + 1 < len && data[i + 1] == '/')
		{
			memset(&out[i], HL_COMMENT, len - i);
			break;
		}

		if(data[i] == '/' && i + 1 < len && data[i + 1] == '*')
		{
			out[i] = (unsigned char)HL_COMMENT;
			out[i + 1] = (unsigned char)HL_COMMENT;
			i += 2;
			in_comment = 1;
			continue;
		}

		if(data[i] == '"')
		{
			out[i] = (unsigned char)HL_STRING;
			i++;

			while(i < len && data[i] != '"')
			{
				out[i] = (unsigned char)HL_STRING;

				if(data[i] == '\\' && i + 1 < len)
				{
					out[i + 1] = (unsigned char)HL_STRING;
					i += 2;
					continue;
				}

				i++;
			}

			if(i < len)
			{
				out[i] = (unsigned char)HL_STRING;
				i++;
			}

			continue;
		}

		if(data[i] == '\'')
		{
			out[i] = (unsigned char)HL_CHAR;
			i++;

			while(i < len && data[i] != '\'')
			{
				out[i] = (unsigned char)HL_CHAR;

				if(data[i] == '\\' && i + 1 < len)
				{
					out[i + 1] = (unsigned char)HL_CHAR;
					i += 2;
					continue;
				}

				i++;
			}

			if(i < len)
			{
				out[i] = (unsigned char)HL_CHAR;
				i++;
			}

			continue;
		}

		if(i == first && data[i] == '#')
		{
			out[i] = (unsigned char)HL_PREPROC;
			i++;

			while(i < len && is_alpha_underscore((unsigned char)data[i]))
			{
				out[i] = (unsigned char)HL_PREPROC;
				i++;
			}

			continue;
		}

		if(data[i] >= '0' && data[i] <= '9')
		{
			while(i < len && (is_word_continue((unsigned char)data[i]) || data[i] == '.'))
			{
				out[i] = (unsigned char)HL_NUMBER;
				i++;
			}

			continue;
		}

		if(is_alpha_underscore((unsigned char)data[i]))
		{
			HlClass cls;

			j = i;

			while(j < len && is_word_continue((unsigned char)data[j]))
			{
				j++;
			}

			cls = HL_NORMAL;

			if(is_keyword(&data[i], j - i))
			{
				cls = HL_KEYWORD;
			}

			memset(&out[i], (int)cls, j - i);
			i = j;
			continue;
		}

		out[i] = (unsigned char)HL_NORMAL;
		i++;
	}

	return in_comment;
}

void highlight_set_filetype(void)
{
	size_t len;

	g_editor.filetype = HL_PLAIN;
	len = strlen(g_editor.filename);

	if(len < 2)
	{
		return;
	}

	if(g_editor.filename[len - 2] != '.')
	{
		return;
	}

	if(g_editor.filename[len - 1] == 'c' || g_editor.filename[len - 1] == 'h')
	{
		g_editor.filetype = HL_C;
	}
}

void highlight_mark_dirty(void)
{
	s_dirty = 1;
}

void highlight_ensure(void)
{
	size_t i;
	int in_comment;

	if(s_dirty == 0)
	{
		return;
	}

	s_dirty = 0;

	if(g_editor.filetype != HL_C)
	{
		return;
	}

	grow_state(g_editor.num_lines);
	in_comment = 0;

	for(i = 0; i < g_editor.num_lines; i++)
	{
		Line *line;

		s_state[i] = (unsigned char)in_comment;
		line = &g_editor.lines[i];

		if(line->data == NULL || line->len == 0)
		{
			continue;
		}

		grow_scratch(line->len);
		in_comment = scan_line(line->data, line->len, in_comment, s_scratch);
	}
}

void highlight_free(void)
{
	free(s_state);
	s_state = NULL;
	s_state_cap = 0;
	free(s_scratch);
	s_scratch = NULL;
	s_scratch_cap = 0;
	s_dirty = 0;
}

void highlight_init_color(void)
{
	s_color = 0;

	if(has_colors() == FALSE)
	{
		return;
	}

	start_color();

	if(use_default_colors() == ERR)
	{
		return;
	}

	init_pair(HL_PAIR_KEYWORD, COLOR_YELLOW, -1);
	init_pair(HL_PAIR_STRING, COLOR_GREEN, -1);
	init_pair(HL_PAIR_COMMENT, COLOR_CYAN, -1);
	init_pair(HL_PAIR_NUMBER, COLOR_MAGENTA, -1);
	init_pair(HL_PAIR_PREPROC, COLOR_RED, -1);
	s_color = 1;
}

int highlight_use_color(void)
{
	return s_color;
}

int highlight_attr(HlClass cls)
{
	if(cls == HL_KEYWORD)
	{
		return COLOR_PAIR(HL_PAIR_KEYWORD) | A_BOLD;
	}

	if(cls == HL_STRING || cls == HL_CHAR)
	{
		return COLOR_PAIR(HL_PAIR_STRING);
	}

	if(cls == HL_COMMENT)
	{
		return COLOR_PAIR(HL_PAIR_COMMENT);
	}

	if(cls == HL_NUMBER)
	{
		return COLOR_PAIR(HL_PAIR_NUMBER);
	}

	if(cls == HL_PREPROC)
	{
		return COLOR_PAIR(HL_PAIR_PREPROC);
	}

	return A_NORMAL;
}

const unsigned char *highlight_classes(size_t row)
{
	Line *line;
	int start_in;

	if(g_editor.filetype != HL_C)
	{
		return NULL;
	}

	if(row >= g_editor.num_lines)
	{
		return NULL;
	}

	line = &g_editor.lines[row];

	if(line->data == NULL || line->len == 0)
	{
		return NULL;
	}

	start_in = 0;

	if(s_state != NULL && row < s_state_cap)
	{
		start_in = s_state[row];
	}

	grow_scratch(line->len);
	scan_line(line->data, line->len, start_in, s_scratch);
	return s_scratch;
}
