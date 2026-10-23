#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "buffer.h"
#include "common.h"
#include "complete.h"
#include "editor.h"
#include "highlight.h"
#include "line.h"
#include "search.h"
#include "undo.h"
#include "util.h"

void editor_ensure_line_cap(Buffer *b, size_t needed)
{
	Line *grown;

	if(b == NULL)
	{
		return;
	}

	if(needed <= b->line_cap)
	{
		return;
	}

	while(b->line_cap < needed)
	{
		b->line_cap *= 2;
	}

	grown = realloc(b->lines, sizeof(Line) * b->line_cap);

	if(grown == NULL)
	{
		die("realloc");
	}

	b->lines = grown;
}

void editor_append_line(Buffer *b, const char *text, size_t len)
{
	Line *line;
	size_t i;

	if(b == NULL)
	{
		return;
	}

	editor_ensure_line_cap(b, b->num_lines + 1);
	line = &b->lines[b->num_lines];
	line_init(line);
	line_ensure_cap(line, len + 1);

	for(i = 0; i < len; i++)
	{
		if(text != NULL)
		{
			line->data[i] = text[i];
		}
	}

	line->len = len;
	line->data[len] = '\0';
	b->num_lines++;
}

void editor_insert_line_at(Buffer *b, size_t at, const char *text, size_t len)
{
	Line *line;

	if(b == NULL)
	{
		return;
	}

	if(at > b->num_lines)
	{
		return;
	}

	editor_ensure_line_cap(b, b->num_lines + 1);
	memmove(&b->lines[at + 1], &b->lines[at], sizeof(Line) * (b->num_lines - at));
	line = &b->lines[at];
	line_init(line);
	line_ensure_cap(line, len + 1);

	if(len > 0 && text != NULL)
	{
		memcpy(line->data, text, len);
	}

	line->len = len;
	line->data[len] = '\0';
	b->num_lines++;
}

const char *editor_eol_name(const Buffer *b)
{
	if(b == NULL)
	{
		return "LF";
	}

	if(b->eol == EOL_CRLF)
	{
		return "CRLF";
	}

	if(b->eol == EOL_MIXED)
	{
		return "MIXED";
	}

	return "LF";
}

static void editor_clear_lines(Buffer *b)
{
	size_t i;

	if(b == NULL)
	{
		return;
	}

	for(i = 0; i < b->num_lines; i++)
	{
		line_free(&b->lines[i]);
	}

	b->num_lines = 0;
}

void editor_open(Buffer *b, const char *filename)
{
	FILE *fp;
	char *buf;
	size_t cap;
	ssize_t read_len;
	size_t crlf_count;
	size_t lf_count;

	if(b == NULL)
	{
		return;
	}

	editor_clear_lines(b);
	b->filename[0] = '\0';
	b->eol = EOL_LF;
	b->trailing_nl = 0;
	b->dirty = 0;

	if(filename == NULL || filename[0] == '\0')
	{
		editor_append_line(b, "", 0);
		highlight_set_filetype(b);
		highlight_mark_dirty(b);
		return;
	}

	snprintf(b->filename, sizeof(b->filename), "%s", filename);
	highlight_set_filetype(b);
	fp = fopen(filename, "r");

	if(fp == NULL)
	{
		editor_append_line(b, "", 0);
		highlight_mark_dirty(b);
		editor_set_status("New file");
		return;
	}

	buf = NULL;
	cap = 0;
	read_len = 0;
	crlf_count = 0;
	lf_count = 0;

	while((read_len = getline(&buf, &cap, fp)) != -1)
	{
		size_t len;
		int has_nl;
		int has_cr;

		len = (size_t)read_len;
		has_nl = 0;
		has_cr = 0;

		if(len > 0 && buf[len - 1] == '\n')
		{
			has_nl = 1;
		}

		if(len >= 2 && buf[len - 1] == '\n' && buf[len - 2] == '\r')
		{
			has_cr = 1;
		}

		if(has_nl && has_cr)
		{
			crlf_count++;
		}
		else if(has_nl)
		{
			lf_count++;
		}

		b->trailing_nl = has_nl;

		while(len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		{
			len--;
		}

		editor_append_line(b, buf, len);
	}

	free(buf);
	fclose(fp);

	if(crlf_count > 0 && lf_count == 0)
	{
		b->eol = EOL_CRLF;
	}
	else if(crlf_count > 0 && lf_count > 0)
	{
		b->eol = EOL_MIXED;
	}
	else
	{
		b->eol = EOL_LF;
	}

	if(b->num_lines == 0)
	{
		editor_append_line(b, "", 0);
	}

	highlight_mark_dirty(b);
	editor_set_status("File loaded");
}

static int editor_write_all(Buffer *b, FILE *fp)
{
	size_t i;

	if(b == NULL || fp == NULL)
	{
		return -1;
	}

	for(i = 0; i < b->num_lines; i++)
	{
		Line *line;
		const char *newline;
		size_t newline_len;

		line = &b->lines[i];

		if(line->len > 0 && fwrite(line->data, 1, line->len, fp) != line->len)
		{
			return -1;
		}

		if(i + 1 >= b->num_lines)
		{
			continue;
		}

		newline = "\n";
		newline_len = 1;

		if(b->eol == EOL_CRLF)
		{
			newline = "\r\n";
			newline_len = 2;
		}

		if(fwrite(newline, 1, newline_len, fp) != newline_len)
		{
			return -1;
		}
	}

	if(b->trailing_nl && b->num_lines > 0)
	{
		const char *final_nl;
		size_t final_len;

		final_nl = "\n";
		final_len = 1;

		if(b->eol == EOL_CRLF)
		{
			final_nl = "\r\n";
			final_len = 2;
		}

		if(fwrite(final_nl, 1, final_len, fp) != final_len)
		{
			return -1;
		}
	}

	return 0;
}

int editor_save(Buffer *b)
{
	FILE *fp;
	char tmp[FILENAME_LIMIT + 16];
	struct stat st;
	int have_mode;
	int fd;

	if(b == NULL)
	{
		return -1;
	}

	if(b->filename[0] == '\0')
	{
		return -1;
	}

	if(snprintf(tmp, sizeof(tmp), "%s.sulf.tmp", b->filename) >= (int)sizeof(tmp))
	{
		editor_set_status("Cannot save: filename too long");
		return -1;
	}

	have_mode = 0;

	if(stat(b->filename, &st) == 0)
	{
		have_mode = 1;
	}

	fp = fopen(tmp, "w");

	if(fp == NULL)
	{
		editor_set_status("Cannot save: fopen failed");
		return -1;
	}

	if(have_mode != 0)
	{
		fd = fileno(fp);

		if(fd >= 0)
		{
			fchmod(fd, st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));
		}
	}

	if(editor_write_all(b, fp) != 0)
	{
		fclose(fp);
		remove(tmp);
		editor_set_status("Cannot save: write failed");
		return -1;
	}

	fd = fileno(fp);

	if(fd < 0 || fsync(fd) != 0)
	{
		fclose(fp);
		remove(tmp);
		editor_set_status("Cannot save: write failed");
		return -1;
	}

	if(fclose(fp) != 0)
	{
		remove(tmp);
		editor_set_status("Cannot save: rename failed");
		return -1;
	}

	if(rename(tmp, b->filename) != 0)
	{
		remove(tmp);
		editor_set_status("Cannot save: rename failed");
		return -1;
	}

	b->dirty = 0;

	if(b->eol == EOL_MIXED)
	{
		b->eol = EOL_LF;
	}

	editor_set_status("File saved");
	return 0;
}

void editor_close_buffer(Buffer *b)
{
	if(b == NULL)
	{
		return;
	}

	editor_clear_lines(b);
	editor_append_line(b, "", 0);
	b->filename[0] = '\0';
	b->dirty = 0;
	b->eol = EOL_LF;
	b->trailing_nl = 0;
	b->filetype = HL_PLAIN;
	highlight_free(b);
	highlight_mark_dirty(b);
	undo_reset(b);
	search_deactivate();
	complete_reset(b);
}
