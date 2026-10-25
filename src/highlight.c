#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "config.h"
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

static unsigned char *s_scratch;
static size_t s_scratch_cap;
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

static void grow_state(Buffer *b, size_t needed)
{
	unsigned char *grown;

	if(b == NULL)
	{
		return;
	}

	if(needed <= b->hl.state_cap)
	{
		return;
	}

	grown = realloc(b->hl.state, needed);

	if(grown == NULL)
	{
		die("realloc");
	}

	b->hl.state = grown;
	b->hl.state_cap = needed;
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

static int scan_c_line(const char *data, size_t len, int in_comment, unsigned char *out)
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

static int is_md_fence(const char *data, size_t len)
{
	size_t i;
	size_t n;
	char f;

	if(data == NULL)
	{
		return 0;
	}

	i = 0;

	while(i < len && (data[i] == ' ' || data[i] == '\t'))
	{
		i++;
	}

	if(i >= len)
	{
		return 0;
	}

	f = data[i];

	if(f != '`' && f != '~')
	{
		return 0;
	}

	n = 0;

	while(i + n < len && data[i + n] == f)
	{
		n++;
	}

	return n >= 3;
}

static size_t md_find_char(const char *data, size_t len, size_t from, char want)
{
	size_t j;

	if(data == NULL)
	{
		return len;
	}

	for(j = from; j < len; j++)
	{
		if(data[j] == want)
		{
			return j;
		}
	}

	return len;
}

static size_t md_find_seq(const char *data, size_t len, size_t from, const char *seq, size_t seqlen)
{
	size_t j;

	if(data == NULL || seq == NULL || seqlen == 0)
	{
		return len;
	}

	for(j = from; j + seqlen <= len; j++)
	{
		if(memcmp(&data[j], seq, seqlen) == 0)
		{
			return j;
		}
	}

	return len;
}

static int scan_md_line(const char *data, size_t len, int in_fence, unsigned char *out)
{
	size_t lead;
	size_t i;

	if(data == NULL || out == NULL)
	{
		return in_fence;
	}

	if(len == 0)
	{
		return in_fence;
	}

	if(is_md_fence(data, len))
	{
		memset(out, HL_STRING, len);
		return in_fence == 0;
	}

	if(in_fence)
	{
		memset(out, HL_STRING, len);
		return 1;
	}

	lead = 0;

	while(lead < len && (data[lead] == ' ' || data[lead] == '\t'))
	{
		lead++;
	}

	if(lead < len && data[lead] == '#')
	{
		size_t h;

		h = lead;

		while(h < len && data[h] == '#')
		{
			h++;
		}

		if(h - lead <= 6 && (h >= len || data[h] == ' ' || data[h] == '\t'))
		{
			memset(out, HL_KEYWORD, len);
			return 0;
		}
	}

	{
		size_t k;
		char mark;
		size_t count;

		k = lead;
		mark = 0;
		count = 0;

		while(k < len)
		{
			if(data[k] == ' ' || data[k] == '\t')
			{
				k++;
				continue;
			}

			if(data[k] != '-' && data[k] != '*' && data[k] != '_')
			{
				break;
			}

			if(mark == 0)
			{
				mark = data[k];
			}

			if(data[k] != mark)
			{
				break;
			}

			count++;
			k++;
		}

		if(mark != 0 && count >= 3 && k >= len)
		{
			memset(out, HL_PREPROC, len);
			return 0;
		}
	}

	i = 0;

	while(i < lead)
	{
		out[i] = (unsigned char)HL_NORMAL;
		i++;
	}

	if(i < len && data[i] == '>')
	{
		while(i < len && data[i] == '>')
		{
			out[i] = (unsigned char)HL_COMMENT;
			i++;
		}

		if(i < len && (data[i] == ' ' || data[i] == '\t'))
		{
			out[i] = (unsigned char)HL_COMMENT;
			i++;
		}
	}

	if(i < len)
	{
		if((data[i] == '-' || data[i] == '+' || data[i] == '*') && i + 1 < len && (data[i + 1] == ' ' || data[i + 1] == '\t'))
		{
			out[i] = (unsigned char)HL_PREPROC;
			i++;
			out[i] = (unsigned char)HL_NORMAL;
			i++;
		}
		else if(data[i] >= '0' && data[i] <= '9')
		{
			size_t d;

			d = i;

			while(d < len && data[d] >= '0' && data[d] <= '9')
			{
				d++;
			}

			if(d < len && data[d] == '.' && d + 1 < len && (data[d + 1] == ' ' || data[d + 1] == '\t'))
			{
				while(i <= d)
				{
					out[i] = (unsigned char)HL_PREPROC;
					i++;
				}

				out[i] = (unsigned char)HL_NORMAL;
				i++;
			}
		}
	}

	while(i < len)
	{
		if(data[i] == '`')
		{
			size_t close;

			close = md_find_char(data, len, i + 1, '`');

			if(close >= len)
			{
				memset(&out[i], HL_STRING, len - i);
				return 0;
			}

			memset(&out[i], HL_STRING, close - i + 1);
			i = close + 1;
			continue;
		}

		if(data[i] == '[')
		{
			size_t rb;
			size_t paren;
			size_t rp;

			rb = md_find_char(data, len, i + 1, ']');

			if(rb + 1 >= len || data[rb + 1] != '(')
			{
				out[i] = (unsigned char)HL_NORMAL;
				i++;
				continue;
			}

			paren = rb + 1;
			rp = md_find_char(data, len, paren + 1, ')');

			if(rp >= len)
			{
				out[i] = (unsigned char)HL_NORMAL;
				i++;
				continue;
			}

			memset(&out[i], HL_KEYWORD, rb - i + 1);
			memset(&out[paren], HL_STRING, rp - paren + 1);
			i = rp + 1;
			continue;
		}

		if((data[i] == '*' && i + 1 < len && data[i + 1] == '*') || (data[i] == '_' && i + 1 < len && data[i + 1] == '_' && (i == 0 || is_word_continue((unsigned char)data[i - 1]) == 0)))
		{
			char seq[2];
			size_t found;

			seq[0] = data[i];
			seq[1] = data[i + 1];
			found = md_find_seq(data, len, i + 2, seq, 2);

			if(found >= len)
			{
				memset(&out[i], HL_NUMBER, len - i);
				return 0;
			}

			memset(&out[i], HL_NUMBER, found - i + 2);
			i = found + 2;
			continue;
		}

		if(data[i] == '*' || (data[i] == '_' && (i == 0 || is_word_continue((unsigned char)data[i - 1]) == 0)))
		{
			size_t found;

			found = i + 1;

			while(found < len)
			{
				found = md_find_char(data, len, found, data[i]);

				if(found >= len)
				{
					break;
				}

				if(data[i] == '*' || found + 1 >= len || is_word_continue((unsigned char)data[found + 1]) == 0)
				{
					break;
				}

				found++;
			}

			if(found >= len)
			{
				memset(&out[i], HL_PREPROC, len - i);
				return 0;
			}

			memset(&out[i], HL_PREPROC, found - i + 1);
			i = found + 1;
			continue;
		}

		out[i] = (unsigned char)HL_NORMAL;
		i++;
	}

	return 0;
}

void highlight_set_filetype(Buffer *b)
{
	size_t len;

	if(b == NULL)
	{
		return;
	}

	b->filetype = HL_PLAIN;
	len = strlen(b->filename);

	if(len >= 3 && strcmp(&b->filename[len - 3], ".md") == 0)
	{
		b->filetype = HL_MD;
		return;
	}

	if(len < 2)
	{
		return;
	}

	if(b->filename[len - 2] != '.')
	{
		return;
	}

	if(b->filename[len - 1] == 'c' || b->filename[len - 1] == 'h')
	{
		b->filetype = HL_C;
	}
}

void highlight_mark_dirty(Buffer *b)
{
	if(b == NULL)
	{
		return;
	}

	b->hl.dirty = 1;
}

void highlight_ensure(Buffer *b)
{
	size_t i;
	int in_span;

	if(b == NULL)
	{
		return;
	}

	if(b->hl.dirty == 0)
	{
		return;
	}

	b->hl.dirty = 0;

	if(b->filetype != HL_C && b->filetype != HL_MD)
	{
		return;
	}

	grow_state(b, b->num_lines);
	in_span = 0;

	for(i = 0; i < b->num_lines; i++)
	{
		Line *line;

		b->hl.state[i] = (unsigned char)in_span;
		line = &b->lines[i];

		if(line->data == NULL || line->len == 0)
		{
			continue;
		}

		grow_scratch(line->len);

		if(b->filetype == HL_MD)
		{
			in_span = scan_md_line(line->data, line->len, in_span, s_scratch);
		}
		else
		{
			in_span = scan_c_line(line->data, line->len, in_span, s_scratch);
		}
	}
}

void highlight_free(Buffer *b)
{
	if(b == NULL)
	{
		return;
	}

	free(b->hl.state);
	b->hl.state = NULL;
	b->hl.state_cap = 0;
	b->hl.dirty = 0;
}

void highlight_init_color(void)
{
	int fg;

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

	if(config_highlight_mono() != 0)
	{
		return;
	}

	fg = config_highlight_fg(CONFIG_HL_KEYWORD);
	init_pair(HL_PAIR_KEYWORD, fg < 0 ? COLOR_YELLOW : fg, -1);
	fg = config_highlight_fg(CONFIG_HL_STRING);
	init_pair(HL_PAIR_STRING, fg < 0 ? COLOR_GREEN : fg, -1);
	fg = config_highlight_fg(CONFIG_HL_COMMENT);
	init_pair(HL_PAIR_COMMENT, fg < 0 ? COLOR_CYAN : fg, -1);
	fg = config_highlight_fg(CONFIG_HL_NUMBER);
	init_pair(HL_PAIR_NUMBER, fg < 0 ? COLOR_MAGENTA : fg, -1);
	fg = config_highlight_fg(CONFIG_HL_PREPROC);
	init_pair(HL_PAIR_PREPROC, fg < 0 ? COLOR_RED : fg, -1);
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

int highlight_git_attr(GitMarker marker)
{
	if(s_color == 0)
	{
		return A_NORMAL;
	}

	if(marker == GIT_ADDED)
	{
		return COLOR_PAIR(HL_PAIR_STRING);
	}

	if(marker == GIT_MODIFIED)
	{
		return COLOR_PAIR(HL_PAIR_KEYWORD);
	}

	if(marker == GIT_DELETED)
	{
		return COLOR_PAIR(HL_PAIR_PREPROC);
	}

	return A_NORMAL;
}

const unsigned char *highlight_classes(Buffer *b, size_t row)
{
	Line *line;
	int start_in;

	if(b == NULL)
	{
		return NULL;
	}

	if(b->filetype != HL_C && b->filetype != HL_MD)
	{
		return NULL;
	}

	if(row >= b->num_lines)
	{
		return NULL;
	}

	line = &b->lines[row];

	if(line->data == NULL || line->len == 0)
	{
		return NULL;
	}

	start_in = 0;

	if(b->hl.state != NULL && row < b->hl.state_cap)
	{
		start_in = b->hl.state[row];
	}

	grow_scratch(line->len);

	if(b->filetype == HL_MD)
	{
		scan_md_line(line->data, line->len, start_in, s_scratch);
	}
	else
	{
		scan_c_line(line->data, line->len, start_in, s_scratch);
	}

	return s_scratch;
}
