#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>
#include "common.h"

/* buffer_poll_disk results: OK (nothing to do), RELOADED (clean buffer
swapped to the on-disk content), CHANGED_KEPT (disk moved but the buffer
is dirty, edits preserved), DELETED (file is gone; caller checks dirty
for the message). Never touches the status line; the caller reports. */
#define DISK_OK 0
#define DISK_RELOADED 1
#define DISK_CHANGED_KEPT 2
#define DISK_DELETED 3

void editor_ensure_line_cap(Buffer *b, size_t needed);
void editor_append_line(Buffer *b, const char *text, size_t len);
void editor_insert_line_at(Buffer *b, size_t at, const char *text, size_t len);
const char *editor_eol_name(const Buffer *b);
void editor_open(Buffer *b, const char *filename);
int editor_save(Buffer *b);
void editor_close_buffer(Buffer *b);
/* stat() the file behind b against its baseline. Clean buffers whose
content actually differs are reloaded (undo reset, markers refreshed).
Dirty buffers and deleted files only update the baseline and report;
identical rewrites (touch) just re-stamp so undo history survives. */
int buffer_poll_disk(Buffer *b);

#endif
