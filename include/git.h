#ifndef GIT_H
#define GIT_H

#include "common.h"

/* Capped hunk table: beyond this many hunks markers switch off
rather than going stale or unbounded. */
#define GIT_MAX_HUNKS 512

typedef enum
{
	GIT_NONE,
	GIT_ADDED,
	GIT_MODIFIED,
	GIT_DELETED
} GitMarker;

typedef struct
{
	int computed;
	int start[GIT_MAX_HUNKS];
	int len[GIT_MAX_HUNKS];
	unsigned char kind[GIT_MAX_HUNKS];
	int count;
	unsigned long epoch;
} GitState;

void git_reset(Buffer *b);
/* Bump the global clock so the next git_refresh recomputes. */
void git_invalidate(void);
/* Recompute markers for b unless already current. Never touches
the screen; safe to call from open/save/switch/focus hooks. */
void git_refresh(Buffer *b);
/* Pure lookup for the draw path: no syscalls, no allocation. */
int git_marker(Buffer *b, size_t file_row);

#endif
