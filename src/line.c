#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "config.h"
#include "line.h"
#include "util.h"

void line_init(Line *line)
{
	if(line == NULL)
	{
		return;
	}

	line->cap = INITIAL_CHAR_CAP;
	line->len = 0;
	line->data = malloc(line->cap);

	if(line->data == NULL)
	{
		die("malloc");
	}

	line->data[0] = '\0';
}

void line_free(Line *line)
{
	if(line == NULL)
	{
		return;
	}

	free(line->data);
	line->data = NULL;
	line->len = 0;
	line->cap = 0;
}

void line_ensure_cap(Line *line, size_t needed)
{
	char *grown;

	if(line == NULL)
	{
		return;
	}

	if(needed <= line->cap)
	{
		return;
	}

	while(line->cap < needed)
	{
		line->cap *= 2;
	}

	grown = realloc(line->data, line->cap);

	if(grown == NULL)
	{
		die("realloc");
	}

	line->data = grown;
}

void line_insert_char(Line *line, size_t at, char c)
{
	if(line == NULL)
	{
		return;
	}

	if(at > line->len)
	{
		return;
	}

	line_ensure_cap(line, line->len + 2);
	memmove(&line->data[at + 1], &line->data[at], line->len - at + 1);
	line->data[at] = c;
	line->len++;
}

void line_delete_char(Line *line, size_t at)
{
	if(line == NULL)
	{
		return;
	}

	if(at >= line->len)
	{
		return;
	}

	memmove(&line->data[at], &line->data[at + 1], line->len - at);
	line->len--;
}

static unsigned int utf8_decode(const char *s, size_t avail, size_t *len_out)
{
	unsigned char c0;
	unsigned char c1;
	unsigned char c2;
	unsigned char c3;
	unsigned int cp;

	if(s == NULL || avail == 0)
	{
		if(len_out != NULL)
		{
			*len_out = 1;
		}

		return 0;
	}

	c0 = (unsigned char)s[0];

	if(c0 < 0x80)
	{
		if(len_out != NULL)
		{
			*len_out = 1;
		}

		return c0;
	}

	if(c0 < 0xC2 || avail < 2)
	{
		if(len_out != NULL)
		{
			*len_out = 1;
		}

		return c0;
	}

	c1 = (unsigned char)s[1];

	if((c1 & 0xC0) != 0x80)
	{
		if(len_out != NULL)
		{
			*len_out = 1;
		}

		return c0;
	}

	if(c0 < 0xE0)
	{
		if(len_out != NULL)
		{
			*len_out = 2;
		}

		return ((unsigned int)(c0 & 0x1F) << 6) | (c1 & 0x3F);
	}

	if(avail < 3)
	{
		if(len_out != NULL)
		{
			*len_out = 1;
		}

		return c0;
	}

	c2 = (unsigned char)s[2];

	if((c2 & 0xC0) != 0x80)
	{
		if(len_out != NULL)
		{
			*len_out = 1;
		}

		return c0;
	}

	if(c0 < 0xF0)
	{
		cp = ((unsigned int)(c0 & 0x0F) << 12) | ((unsigned int)(c1 & 0x3F) << 6) | (c2 & 0x3F);

		if(cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))
		{
			if(len_out != NULL)
			{
				*len_out = 1;
			}

			return c0;
		}

		if(len_out != NULL)
		{
			*len_out = 3;
		}

		return cp;
	}

	if(avail < 4)
	{
		if(len_out != NULL)
		{
			*len_out = 1;
		}

		return c0;
	}

	c3 = (unsigned char)s[3];

	if((c3 & 0xC0) != 0x80)
	{
		if(len_out != NULL)
		{
			*len_out = 1;
		}

		return c0;
	}

	cp = ((unsigned int)(c0 & 0x07) << 18) | ((unsigned int)(c1 & 0x3F) << 12) |
		((unsigned int)(c2 & 0x3F) << 6) | (c3 & 0x3F);

	if(cp < 0x10000 || cp > 0x10FFFF)
	{
		if(len_out != NULL)
		{
			*len_out = 1;
		}

		return c0;
	}

	if(len_out != NULL)
	{
		*len_out = 4;
	}

	return cp;
}

static int utf8_code_width(unsigned int cp)
{
	if(cp < 0x20 || (cp >= 0x7F && cp <= 0x9F))
	{
		return 1;
	}

	if((cp >= 0x300 && cp <= 0x36F) || (cp >= 0x483 && cp <= 0x489) ||
		(cp >= 0x591 && cp <= 0x5C7) || (cp >= 0x610 && cp <= 0x61A) ||
		(cp >= 0x64B && cp <= 0x65F) || cp == 0x670 ||
		(cp >= 0x6D6 && cp <= 0x6ED) || cp == 0x711 ||
		(cp >= 0x730 && cp <= 0x74A) || (cp >= 0x7A6 && cp <= 0x7B0) ||
		(cp >= 0x900 && cp <= 0x903) || cp == 0x93C || (cp >= 0x93E && cp <= 0x94D) ||
		(cp >= 0x951 && cp <= 0x957) || (cp >= 0x20D0 && cp <= 0x20FF) ||
		(cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xFE20 && cp <= 0xFE2F) ||
		(cp >= 0xE0100 && cp <= 0xE01EF))
	{
		return 0;
	}

	if((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x231A && cp <= 0x231B) ||
		(cp >= 0x23E9 && cp <= 0x23FC) || (cp >= 0x23F8 && cp <= 0x23FA) ||
		(cp >= 0x2329 && cp <= 0x232A) || (cp >= 0x2E80 && cp <= 0x303E) ||
		(cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
		(cp >= 0x4E00 && cp <= 0xA4CF) || (cp >= 0xA960 && cp <= 0xA97C) ||
		(cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
		(cp >= 0xFE10 && cp <= 0xFE19) || (cp >= 0xFE30 && cp <= 0xFE4F) ||
		(cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6) ||
		(cp >= 0x1F300 && cp <= 0x1FAFF) || (cp >= 0x20000 && cp <= 0x3FFFD))
	{
		return 2;
	}

	return 1;
}

size_t utf8_char_len(const char *s, size_t avail)
{
	size_t len;

	utf8_decode(s, avail, &len);
	return len;
}

int utf8_char_width(const char *s, size_t avail)
{
	return utf8_code_width(utf8_decode(s, avail, NULL));
}

int utf8_width_n(const char *s, size_t n)
{
	size_t i;
	int width;

	if(s == NULL)
	{
		return 0;
	}

	i = 0;
	width = 0;

	while(i < n && s[i] != '\0')
	{
		size_t len;

		len = utf8_char_len(&s[i], n - i);
		width += utf8_code_width(utf8_decode(&s[i], n - i, NULL));

		if(len == 0)
		{
			break;
		}

		i += len;
	}

	return width;
}

size_t utf8_fit_cells(const char *s, size_t n, int max_cells)
{
	size_t i;
	int used;

	if(s == NULL || max_cells <= 0)
	{
		return 0;
	}

	i = 0;
	used = 0;

	while(i < n && s[i] != '\0')
	{
		size_t len;
		int w;

		len = utf8_char_len(&s[i], n - i);
		w = utf8_code_width(utf8_decode(&s[i], n - i, NULL));

		if(len == 0 || used + w > max_cells)
		{
			break;
		}

		used += w;

		if(len > n - i)
		{
			break;
		}

		i += len;
	}

	return i;
}

size_t line_char_start(const Line *line, size_t idx)
{
	if(line == NULL || line->data == NULL)
	{
		return 0;
	}

	if(idx > line->len)
	{
		idx = line->len;
	}

	while(idx > 0 && ((unsigned char)line->data[idx] & 0xC0) == 0x80)
	{
		idx--;
	}

	return idx;
}

size_t line_char_next(const Line *line, size_t idx)
{
	size_t len;

	if(line == NULL || line->data == NULL)
	{
		return idx;
	}

	idx = line_char_start(line, idx);

	if(idx >= line->len)
	{
		return line->len;
	}

	len = utf8_char_len(&line->data[idx], line->len - idx);

	if(len == 0 || idx + len > line->len)
	{
		return line->len;
	}

	return idx + len;
}

static int line_tab_advance(int col)
{
	int next;
	int width;

	width = config_tab_width();

	if(width < CONFIG_MIN_TAB_WIDTH || width > CONFIG_MAX_TAB_WIDTH)
	{
		width = CONFIG_DEFAULT_TAB_WIDTH;
	}

	if(col < 0)
	{
		col = 0;
	}

	next = ((col / width) + 1) * width;

	return next - col;
}

size_t line_cells_upto(const Line *line, size_t upto)
{
	size_t i;
	int width;

	if(line == NULL || line->data == NULL)
	{
		return 0;
	}

	if(upto > line->len)
	{
		upto = line->len;
	}

	i = 0;
	width = 0;

	while(i < upto)
	{
		size_t len;

		if(line->data[i] == '\t')
		{
			width += line_tab_advance(width);
			i++;
			continue;
		}

		len = utf8_char_len(&line->data[i], line->len - i);

		if(len == 0 || i + len > line->len)
		{
			break;
		}

		width += utf8_char_width(&line->data[i], line->len - i);
		i += len;
	}

	return (size_t)(width < 0 ? 0 : width);
}

size_t line_byte_at_cell(const Line *line, size_t start, int cells)
{
	size_t i;
	int col;

	if(line == NULL || line->data == NULL)
	{
		return start;
	}

	if(start > line->len)
	{
		start = line->len;
	}

	i = line_char_start(line, start);
	col = (int)line_cells_upto(line, i);

	while(i < line->len && cells > 0)
	{
		int w;

		if(line->data[i] == '\t')
		{
			w = line_tab_advance(col);
		}
		else
		{
			w = utf8_char_width(&line->data[i], line->len - i);
		}

		if(w > cells)
		{
			break;
		}

		cells -= w;
		col += w;
		i = line_char_next(line, i);
	}

	return i;
}
