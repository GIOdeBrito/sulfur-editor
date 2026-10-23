#ifndef UNDO_H
#define UNDO_H

#include <stddef.h>
#include "common.h"

#include "line.h"

#define UNDO_OTHER 0
#define UNDO_INSERT 1
#define UNDO_ERASE 2

#define UNDO_LIMIT 64

typedef struct
{
	Line *lines;
	size_t num_lines;
	size_t cx;
	size_t cy;
	size_t want_cx;
	int sel_active;
	size_t sel_ax;
	size_t sel_ay;
	int dirty;
} UndoSlot;

typedef struct
{
	UndoSlot undo[UNDO_LIMIT];
	size_t undo_count;
	UndoSlot redo[UNDO_LIMIT];
	size_t redo_count;
	int kind;
	size_t cy;
	size_t cx;
} UndoState;

void undo_reset(Buffer *b);
void undo_free(Buffer *b);
void undo_begin(Buffer *b, View *v, int kind);
void undo_end(Buffer *b, View *v, int kind);
int undo_undo(Buffer *b, View *v);
int undo_redo(Buffer *b, View *v);

#endif
