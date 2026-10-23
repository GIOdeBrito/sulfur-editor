#ifndef TABS_H
#define TABS_H

#include "editor.h"
#include "common.h"

#define MAX_BUFFERS 16

void tabs_init(void);
void tabs_free(void);
int tabs_count(void);
int tabs_active_index(void);
void tabs_activate(int i);
Buffer *tabs_active(void);
View *tabs_view(void);
Buffer *tabs_get(int i);
/* Open-or-switch: an existing tab with the same path is activated.
Otherwise the file loads into the pristine current tab, or a fresh
tab. Returns NULL when no slot is free or path is bad. */
Buffer *tabs_open_file(const char *path);
int tabs_next(void);
int tabs_prev(void);
/* Closes the active tab. Refuses dirty tabs (status message) and
resets the last tab to a fresh unnamed buffer. Returns 1 on close. */
int tabs_close_current(void);
void tabs_draw_bar(void);
/* Maps a screen x on the tab bar row to a tab index, or -1. */
int tabs_hit_tab(int x);

#endif
