#ifndef COMPLETE_H
#define COMPLETE_H

#include <stddef.h>
#include "common.h"

#define COMPLETE_MIN_PREFIX 2
#define COMPLETE_MAX_WORD 64
#define COMPLETE_MAX_ITEMS 32

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
} CompleteState;

void complete_reset(Buffer *b);
int complete_is_active(Buffer *b);
void complete_update(Buffer *b, View *v);
void complete_update_forced(Buffer *b, View *v);
void complete_move(Buffer *b, int delta);
void complete_accept(Buffer *b, View *v);
int complete_handle_key(Buffer *b, View *v, int key);
void complete_draw(Buffer *b, View *v);

#endif
