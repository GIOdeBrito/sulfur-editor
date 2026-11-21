#ifndef MOUSE_H
#define MOUSE_H

#include "common.h"

void mouse_init(void);
void mouse_shutdown(void);
void mouse_cancel_drag(void);
int mouse_handle(Buffer *b, View *v);

#endif
