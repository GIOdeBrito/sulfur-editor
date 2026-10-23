#ifndef SEARCH_H
#define SEARCH_H

#include <stddef.h>
#include "common.h"

#define SEARCH_MAX_MATCHES 512

typedef struct
{
	size_t row;
	size_t col;
} SearchMatch;

typedef struct
{
	SearchMatch matches[SEARCH_MAX_MATCHES];
	size_t count;
	size_t current;
	int truncated;
} SearchState;

void search_reset(void);
void search_deactivate(void);
int search_is_active(void);
void search_prompt_and_find(Buffer *b, View *v);
void search_next(Buffer *b, View *v, int direction);
int search_handle_key(Buffer *b, View *v, int key);
int search_cover(Buffer *b, size_t row, size_t col, int *is_current);

#endif
