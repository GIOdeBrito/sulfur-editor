#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define SULF_VERSION "1.0.0"
#define TAB_WIDTH 4
#define FILENAME_LIMIT 512
#define STATUS_LIMIT 256
#define CLIPBOARD_LIMIT 8192
#define INITIAL_LINE_CAP 16
#define INITIAL_CHAR_CAP 64

/* Forward declarations for the editor state owned by editor.h.
Every module operates on these by pointer; tabs.c owns the instances. */
typedef struct Buffer Buffer;
typedef struct View View;

#endif
