#ifndef EXPLORER_H
#define EXPLORER_H

void explorer_open(const char *start_dir);
void explorer_reset(void);
int explorer_is_active(void);
int explorer_handle_key(int key);
void explorer_draw(void);
void explorer_dim_background(void);
int explorer_cursor_x(void);
int explorer_cursor_y(void);

#endif
