#ifndef SEARCH_H
#define SEARCH_H

#include <stddef.h>

void search_reset(void);
void search_deactivate(void);
int search_is_active(void);
void search_prompt_and_find(void);
void search_next(int direction);
int search_handle_key(int key);
int search_cover(size_t row, size_t col, int *is_current);

#endif
