#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "buffer.h"
#include "common.h"
#include "editor.h"
#include "highlight.h"
#include "line.h"
#include "util.h"

void editor_ensure_line_cap(size_t needed)
{
	Line *grown;

	if(needed <= g_editor.line_cap)
	{
		return;
	}

	while(g_editor.line_cap < needed)
	{
		g_editor.line_cap *= 2;
	}

	grown = realloc(g_editor.lines, sizeof(Line) * g_editor.line_cap);

	if(grown == NULL)
	{
		die("realloc");
	}

	g_editor.lines = grown;
}

void editor_append_line(const char *text, size_t len)
{
	Line *line;
	size_t expanded;
	size_t i;
	size_t j;

	editor_ensure_line_cap(g_editor.num_lines + 1);
	line = &g_editor.lines[g_editor.num_lines];
	line_init(line);
	expanded = 0;

	for(i = 0; i < len; i++)
	{
		if(text != NULL && text[i] == '\t')
		{
			expanded += (size_t)TAB_WIDTH;
		}
		else
		{
			expanded++;
		}
	}

	line_ensure_cap(line, expanded + 1);
	j = 0;

	for(i = 0; i < len; i++)
	{
		int k;

		if(text != NULL && text[i] == '\t')
		{
			for(k = 0; k < TAB_WIDTH; k++)
			{
				line->data[j] = ' ';
				j++;
			}

			continue;
		}

		if(text != NULL)
		{
			line->data[j] = text[i];
			j++;
		}
	}

	line->len = expanded;
	line->data[expanded] = '\0';
	g_editor.num_lines++;
}

void editor_insert_line_at(size_t at, const char *text, size_t len)
{
	Line *line;

	if(at > g_editor.num_lines)
	{
		return;
	}

	editor_ensure_line_cap(g_editor.num_lines + 1);
	memmove(&g_editor.lines[at + 1], &g_editor.lines[at], sizeof(Line) * (g_editor.num_lines - at));
	line = &g_editor.lines[at];
	line_init(line);
	line_ensure_cap(line, len + 1);

	if(len > 0 && text != NULL)
	{
		memcpy(line->data, text, len);
	}

	line->len = len;
	line->data[len] = '\0';
	g_editor.num_lines++;
}

const char *editor_eol_name(void)
{
	if(g_editor.eol == EOL_CRLF)
	{
		return "CRLF";
	}

	if(g_editor.eol == EOL_MIXED)
	{
		return "MIXED";
	}

	return "LF";
}

void editor_open(const char *filename)
{
	FILE *fp;
	char *buf;
	size_t cap;
	ssize_t read_len;
	size_t crlf_count;
	size_t lf_count;

	if(filename == NULL || filename[0] == '\0')
	{
		g_editor.eol = EOL_LF;
		g_editor.trailing_nl = 0;
		editor_append_line("", 0);
		highlight_set_filetype();
		highlight_mark_dirty();
		return;
	}

	snprintf(g_editor.filename, sizeof(g_editor.filename), "%s", filename);
	highlight_set_filetype();
	fp = fopen(filename, "r");

	if(fp == NULL)
	{
		g_editor.eol = EOL_LF;
		g_editor.trailing_nl = 0;
		editor_append_line("", 0);
		highlight_mark_dirty();
		editor_set_status("New file");
		return;
	}

	buf = NULL;
	cap = 0;
	read_len = 0;
	crlf_count = 0;
	lf_count = 0;
	g_editor.trailing_nl = 0;

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

		g_editor.trailing_nl = has_nl;

		while(len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		{
			len--;
		}

		editor_append_line(buf, len);
	}

	free(buf);
	fclose(fp);

	if(crlf_count > 0 && lf_count == 0)
	{
		g_editor.eol = EOL_CRLF;
	}
	else if(crlf_count > 0 && lf_count > 0)
	{
		g_editor.eol = EOL_MIXED;
	}
	else
	{
		g_editor.eol = EOL_LF;
	}

	if(g_editor.num_lines == 0)
	{
		editor_append_line("", 0);
	}

	g_editor.dirty = 0;
	highlight_mark_dirty();
	editor_set_status("File loaded");
}

static int editor_write_all(FILE *fp)
{
	size_t i;

	if(fp == NULL)
	{
		return -1;
	}

	for(i = 0; i < g_editor.num_lines; i++)
	{
		Line *line;
		const char *newline;
		size_t newline_len;

		line = &g_editor.lines[i];

		if(line->len > 0 && fwrite(line->data, 1, line->len, fp) != line->len)
		{
			return -1;
		}

		if(i + 1 >= g_editor.num_lines)
		{
			continue;
		}

		newline = "\n";
		newline_len = 1;

		if(g_editor.eol == EOL_CRLF)
		{
			newline = "\r\n";
			newline_len = 2;
		}

		if(fwrite(newline, 1, newline_len, fp) != newline_len)
		{
			return -1;
		}
	}

	if(g_editor.trailing_nl && g_editor.num_lines > 0)
	{
		const char *final_nl;
		size_t final_len;

		final_nl = "\n";
		final_len = 1;

		if(g_editor.eol == EOL_CRLF)
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

int editor_save(void)
{
	FILE *fp;
	char tmp[FILENAME_LIMIT + 16];
	struct stat st;
	int have_mode;
	int fd;

	if(g_editor.filename[0] == '\0')
	{
		return -1;
	}

	if(snprintf(tmp, sizeof(tmp), "%s.sulf.tmp", g_editor.filename) >= (int)sizeof(tmp))
	{
		editor_set_status("Cannot save: filename too long");
		return -1;
	}

	have_mode = 0;

	if(stat(g_editor.filename, &st) == 0)
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

	if(editor_write_all(fp) != 0)
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
		editor_set_status("Cannot save: write failed");
		return -1;
	}

	if(rename(tmp, g_editor.filename) != 0)
	{
		remove(tmp);
		editor_set_status("Cannot save: rename failed");
		return -1;
	}

	g_editor.dirty = 0;

	if(g_editor.eol == EOL_MIXED)
	{
		g_editor.eol = EOL_LF;
	}

	editor_set_status("File saved");
	return 0;
}
