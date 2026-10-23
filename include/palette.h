#ifndef PALETTE_H
#define PALETTE_H

void palette_open(void);
void palette_reset(void);
int palette_is_active(void);
int palette_handle_key(int key);
void palette_draw(void);
void palette_dim_background(void);
int palette_query_cursor_x(void);
int palette_query_cursor_y(void);

#endif
