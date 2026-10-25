#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "editor.h"
#include "git.h"
#include "highlight.h"
#include "line.h"
#include "undo.h"
#include "util.h"

/* Full-buffer snapshots. Simple and uniform across every mutation path
(selection replace, indent, completion accept); capped so worst-case
memory stays bounded. Typing runs coalesce via undo_begin/undo_end.
Stacks live in the Buffer so tabs keep independent histories. */

static void undo_copy_lines(Line *dst, const Line *src, size_t n)
{
	size_t i;

	for(i = 0; i < n; i++)
	{
		line_init(&dst[i]);
		line_ensure_cap(&dst[i], src[i].len + 1);

		if(src[i].len > 0 && src[i].data != NULL)
		{
			memcpy(dst[i].data, src[i].data, src[i].len);
		}

		dst[i].len = src[i].len;
		dst[i].data[dst[i].len] = '\0';
	}
}

static void undo_state_free(UndoSlot *state)
{
	size_t i;

	if(state == NULL)
	{
		return;
	}

	if(state->lines != NULL)
	{
		for(i = 0; i < state->num_lines; i++)
		{
			line_free(&state->lines[i]);
		}

		free(state->lines);
		state->lines = NULL;
	}

	state->num_lines = 0;
}

static void undo_state_capture(Buffer *b, View *v, UndoSlot *state)
{
	if(b == NULL || v == NULL || state == NULL)
	{
		return;
	}

	undo_state_free(state);
	state->num_lines = b->num_lines;
	state->cx = v->cx;
	state->cy = v->cy;
	state->want_cx = v->want_cx;
	state->sel_active = v->sel_active;
	state->sel_ax = v->sel_ax;
	state->sel_ay = v->sel_ay;
	state->dirty = b->dirty;
	state->lines = NULL;

	if(b->num_lines == 0)
	{
		return;
	}

	state->lines = malloc(sizeof(Line) * b->num_lines);

	if(state->lines == NULL)
	{
		die("malloc");
	}

	undo_copy_lines(state->lines, b->lines, b->num_lines);
}

static void undo_state_restore(Buffer *b, View *v, const UndoSlot *state)
{
	size_t i;
	size_t cap;

	if(b == NULL || v == NULL || state == NULL)
	{
		return;
	}

	for(i = 0; i < b->num_lines; i++)
	{
		line_free(&b->lines[i]);
	}

	free(b->lines);
	b->lines = NULL;
	b->num_lines = 0;

	cap = state->num_lines;

	if(cap < INITIAL_LINE_CAP)
	{
		cap = INITIAL_LINE_CAP;
	}

	b->lines = malloc(sizeof(Line) * cap);

	if(b->lines == NULL)
	{
		die("malloc");
	}

	b->line_cap = cap;
	undo_copy_lines(b->lines, state->lines, state->num_lines);
	b->num_lines = state->num_lines;
	v->cx = state->cx;
	v->cy = state->cy;
	v->want_cx = state->want_cx;
	v->sel_active = state->sel_active;
	v->sel_ax = state->sel_ax;
	v->sel_ay = state->sel_ay;
	b->dirty = state->dirty;
	editor_clamp_cursor(b, v);
	highlight_mark_dirty(b);
	git_touch(b);
}

static void undo_stack_clear(UndoSlot *stack, size_t *count)
{
	size_t i;

	if(stack == NULL || count == NULL)
	{
		return;
	}

	for(i = 0; i < *count; i++)
	{
		undo_state_free(&stack[i]);
	}

	*count = 0;
}

static void undo_stack_push(Buffer *b, View *v, UndoSlot *stack, size_t *count)
{
	if(b == NULL || v == NULL || stack == NULL || count == NULL)
	{
		return;
	}

	if(*count >= UNDO_LIMIT)
	{
		undo_state_free(&stack[0]);
		memmove(&stack[0], &stack[1], sizeof(UndoSlot) * (UNDO_LIMIT - 1));
		(*count)--;
		stack[*count].lines = NULL;
		stack[*count].num_lines = 0;
	}

	undo_state_capture(b, v, &stack[*count]);
	(*count)++;
}

void undo_reset(Buffer *b)
{
	if(b == NULL)
	{
		return;
	}

	undo_stack_clear(b->undo.undo, &b->undo.undo_count);
	undo_stack_clear(b->undo.redo, &b->undo.redo_count);
	b->undo.kind = UNDO_OTHER;
	b->undo.cy = 0;
	b->undo.cx = 0;
}

void undo_free(Buffer *b)
{
	if(b == NULL)
	{
		return;
	}

	undo_reset(b);
}

void undo_begin(Buffer *b, View *v, int kind)
{
	if(b == NULL || v == NULL)
	{
		return;
	}

	if(kind != UNDO_OTHER && kind == b->undo.kind && v->cy == b->undo.cy && v->cx == b->undo.cx)
	{
		return;
	}

	undo_stack_push(b, v, b->undo.undo, &b->undo.undo_count);
	undo_stack_clear(b->undo.redo, &b->undo.redo_count);
}

void undo_end(Buffer *b, View *v, int kind)
{
	if(b == NULL || v == NULL)
	{
		return;
	}

	b->undo.kind = kind;
	b->undo.cy = v->cy;
	b->undo.cx = v->cx;
}

int undo_undo(Buffer *b, View *v)
{
	if(b == NULL || v == NULL)
	{
		return 0;
	}

	if(b->undo.undo_count == 0)
	{
		editor_set_status("Nothing to undo");
		return 0;
	}

	undo_stack_push(b, v, b->undo.redo, &b->undo.redo_count);
	b->undo.undo_count--;
	undo_state_restore(b, v, &b->undo.undo[b->undo.undo_count]);
	b->undo.kind = UNDO_OTHER;
	editor_set_status("Undid change");
	return 1;
}

int undo_redo(Buffer *b, View *v)
{
	if(b == NULL || v == NULL)
	{
		return 0;
	}

	if(b->undo.redo_count == 0)
	{
		editor_set_status("Nothing to redo");
		return 0;
	}

	undo_stack_push(b, v, b->undo.undo, &b->undo.undo_count);
	b->undo.redo_count--;
	undo_state_restore(b, v, &b->undo.redo[b->undo.redo_count]);
	b->undo.kind = UNDO_OTHER;
	editor_set_status("Redid change");
	return 1;
}
