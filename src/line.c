#include <stdlib.h>
#include <string.h>

#include "common.h"
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
