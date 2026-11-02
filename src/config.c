#include <ctype.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "config.h"

/* Config file (`~/.config/sulf` TODO entry): plain `key = value` lines,
no sections, no dependencies. Fixed buffers only, no malloc. Missing
file keeps defaults silently; bad lines are ignored with a notice. */

#define CONFIG_LINE_LIMIT 256
#define CONFIG_KEY_LIMIT 64
#define CONFIG_VALUE_LIMIT 192
#define CONFIG_PATH_LIMIT 1024
#define CONFIG_MAX_ACTIONS 40
#define CONFIG_MAX_USER 40

typedef struct
{
	const char *action;
	int keys[CONFIG_MAX_KEYS_PER_ACTION];
	int nkeys;
} ConfigDefault;

typedef struct
{
	char action[32];
	int keys[CONFIG_MAX_KEYS_PER_ACTION];
	int nkeys;
} ConfigEntry;

/* Defaults reproduce today's editor_handle_key bindings exactly,
including the multi-key branches (palette/home/end/backspace/newline).
Shift+Up/Down arrive as KEY_SR/KEY_SF (no KEY_SUP/KEY_SDOWN). */
static const ConfigDefault s_defaults[] =
{
	{ "quit", { CTRL_KEY('q') }, 1 },
	{ "save", { CTRL_KEY('s') }, 1 },
	{ "undo", { CTRL_KEY('z') }, 1 },
	{ "redo", { CTRL_KEY('y') }, 1 },
	{ "copy", { CTRL_KEY('c') }, 1 },
	{ "cut", { CTRL_KEY('x') }, 1 },
	{ "paste", { CTRL_KEY('v') }, 1 },
	{ "find", { CTRL_KEY('f') }, 1 },
	{ "palette", { CTRL_KEY('p'), KEY_F(1) }, 2 },
	{ "explorer", { KEY_F(2) }, 1 },
	{ "next-tab", { CTRL_KEY(']') }, 1 },
	{ "prev-tab", { CTRL_KEY('^') }, 1 },
	{ "left", { KEY_LEFT }, 1 },
	{ "right", { KEY_RIGHT }, 1 },
	{ "up", { KEY_UP }, 1 },
	{ "down", { KEY_DOWN }, 1 },
	{ "sel-left", { KEY_SLEFT }, 1 },
	{ "sel-right", { KEY_SRIGHT }, 1 },
	{ "sel-up", { KEY_SR }, 1 },
	{ "sel-down", { KEY_SF }, 1 },
	{ "page-up", { KEY_PPAGE }, 1 },
	{ "page-down", { KEY_NPAGE }, 1 },
	{ "sel-page-up", { KEY_SPREVIOUS }, 1 },
	{ "sel-page-down", { KEY_SNEXT }, 1 },
	{ "home", { KEY_HOME, CTRL_KEY('a') }, 2 },
	{ "end", { KEY_END, CTRL_KEY('e') }, 2 },
	{ "sel-home", { KEY_SHOME }, 1 },
	{ "sel-end", { KEY_SEND }, 1 },
	{ "unindent", { KEY_BTAB }, 1 },
	{ "backspace", { KEY_BACKSPACE, 127, CTRL_KEY('h') }, 3 },
	{ "delete", { KEY_DC }, 1 },
	{ "newline", { '\n', '\r', KEY_ENTER }, 3 },
	{ "force-complete", { CTRL_KEY(' ') }, 1 },
	{ "tab", { '\t' }, 1 },
};

static int s_tab_width = CONFIG_DEFAULT_TAB_WIDTH;
static int s_fg[CONFIG_HL_COUNT] = { 156, 108, 243, 223, 149, 250, 234 };
static int s_fg_set[CONFIG_HL_COUNT] = { 0, 0, 0, 0, 0 };
static int s_mono = 0;
static int s_acid = 1;
static char s_notice[128] = { 0 };
static ConfigEntry s_resolved[CONFIG_MAX_ACTIONS];
static int s_resolved_count = 0;
static ConfigEntry s_user[CONFIG_MAX_USER];
static int s_user_count = 0;
static int s_loaded = 0;

static void config_note(const char *msg)
{
	if(msg == NULL)
	{
		return;
	}

	if(s_notice[0] != '\0')
	{
		return;
	}

	snprintf(s_notice, sizeof(s_notice), "%s", msg);
}

static void config_lower(char *s)
{
	size_t i;

	if(s == NULL)
	{
		return;
	}

	for(i = 0; s[i] != '\0'; i++)
	{
		s[i] = (char)tolower((unsigned char)s[i]);
	}
}

static char *config_trim(char *s)
{
	size_t len;

	if(s == NULL)
	{
		return NULL;
	}

	while(*s == ' ' || *s == '\t')
	{
		s++;
	}

	len = strlen(s);

	while(len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
		s[len - 1] == '\n' || s[len - 1] == '\r'))
	{
		s[--len] = '\0';
	}

	return s;
}

static int config_class_index(const char *name)
{
	if(name == NULL)
	{
		return -1;
	}

	if(strcmp(name, "keyword") == 0)
	{
		return CONFIG_HL_KEYWORD;
	}

	if(strcmp(name, "string") == 0)
	{
		return CONFIG_HL_STRING;
	}

	if(strcmp(name, "comment") == 0)
	{
		return CONFIG_HL_COMMENT;
	}

	if(strcmp(name, "number") == 0)
	{
		return CONFIG_HL_NUMBER;
	}

	if(strcmp(name, "preproc") == 0)
	{
		return CONFIG_HL_PREPROC;
	}

	if(strcmp(name, "text") == 0)
	{
		return CONFIG_HL_TEXT;
	}

	if(strcmp(name, "background") == 0)
	{
		return CONFIG_HL_BG;
	}

	return -1;
}

/* Named ncurses colors 0..7, 256-palette 0..255 by number,
-1 for default/transparent. */
static int config_parse_color(const char *name, int *out)
{
	if(name == NULL || out == NULL)
	{
		return -1;
	}

	if(strcmp(name, "black") == 0)
	{
		*out = 0;
		return 0;
	}

	if(strcmp(name, "red") == 0)
	{
		*out = 1;
		return 0;
	}

	if(strcmp(name, "green") == 0)
	{
		*out = 2;
		return 0;
	}

	if(strcmp(name, "yellow") == 0)
	{
		*out = 3;
		return 0;
	}

	if(strcmp(name, "blue") == 0)
	{
		*out = 4;
		return 0;
	}

	if(strcmp(name, "magenta") == 0)
	{
		*out = 5;
		return 0;
	}

	if(strcmp(name, "cyan") == 0)
	{
		*out = 6;
		return 0;
	}

	if(strcmp(name, "white") == 0)
	{
		*out = 7;
		return 0;
	}

	if(strcmp(name, "default") == 0 || strcmp(name, "none") == 0)
	{
		*out = -1;
		return 0;
	}

	{
		char *end;
		long v;

		v = strtol(name, &end, 10);

		if(end != name && *end == '\0' && v >= 0 && v <= 255)
		{
			*out = (int)v;
			return 0;
		}
	}

	return -1;
}

static int config_named_key(const char *name, int *out)
{
	long fn;
	char *end;

	if(name == NULL || out == NULL)
	{
		return -1;
	}

	if(strcmp(name, "tab") == 0)
	{
		*out = '\t';
		return 0;
	}

	if(strcmp(name, "enter") == 0)
	{
		*out = '\n';
		return 0;
	}

	if(strcmp(name, "return") == 0 || strcmp(name, "cr") == 0)
	{
		*out = '\r';
		return 0;
	}

	if(strcmp(name, "keyenter") == 0)
	{
		*out = KEY_ENTER;
		return 0;
	}

	if(strcmp(name, "esc") == 0 || strcmp(name, "escape") == 0)
	{
		*out = 27;
		return 0;
	}

	if(strcmp(name, "backspace") == 0)
	{
		*out = KEY_BACKSPACE;
		return 0;
	}

	if(strcmp(name, "del") == 0)
	{
		*out = 127;
		return 0;
	}

	if(strcmp(name, "delete") == 0)
	{
		*out = KEY_DC;
		return 0;
	}

	if(strcmp(name, "space") == 0)
	{
		*out = ' ';
		return 0;
	}

	if(strcmp(name, "up") == 0)
	{
		*out = KEY_UP;
		return 0;
	}

	if(strcmp(name, "down") == 0)
	{
		*out = KEY_DOWN;
		return 0;
	}

	if(strcmp(name, "left") == 0)
	{
		*out = KEY_LEFT;
		return 0;
	}

	if(strcmp(name, "right") == 0)
	{
		*out = KEY_RIGHT;
		return 0;
	}

	if(strcmp(name, "home") == 0)
	{
		*out = KEY_HOME;
		return 0;
	}

	if(strcmp(name, "end") == 0)
	{
		*out = KEY_END;
		return 0;
	}

	if(strcmp(name, "pgup") == 0 || strcmp(name, "pageup") == 0)
	{
		*out = KEY_PPAGE;
		return 0;
	}

	if(strcmp(name, "pgdn") == 0 || strcmp(name, "pagedn") == 0 ||
		strcmp(name, "pagedown") == 0)
	{
		*out = KEY_NPAGE;
		return 0;
	}

	if(strcmp(name, "shift-tab") == 0)
	{
		*out = KEY_BTAB;
		return 0;
	}

	if(strcmp(name, "shift-left") == 0)
	{
		*out = KEY_SLEFT;
		return 0;
	}

	if(strcmp(name, "shift-right") == 0)
	{
		*out = KEY_SRIGHT;
		return 0;
	}

	if(strcmp(name, "shift-up") == 0)
	{
		*out = KEY_SR;
		return 0;
	}

	if(strcmp(name, "shift-down") == 0)
	{
		*out = KEY_SF;
		return 0;
	}

	if(strcmp(name, "shift-home") == 0)
	{
		*out = KEY_SHOME;
		return 0;
	}

	if(strcmp(name, "shift-end") == 0)
	{
		*out = KEY_SEND;
		return 0;
	}

	if(strcmp(name, "shift-pgup") == 0 || strcmp(name, "shift-pageup") == 0)
	{
		*out = KEY_SPREVIOUS;
		return 0;
	}

	if(strcmp(name, "shift-pgdn") == 0 || strcmp(name, "shift-pagedown") == 0)
	{
		*out = KEY_SNEXT;
		return 0;
	}

	if(name[0] == 'f' && name[1] != '\0')
	{
		fn = strtol(name + 1, &end, 10);

		if(end != name + 1 && *end == '\0' && fn >= 1 && fn <= 12)
		{
			*out = KEY_F(fn);
			return 0;
		}
	}

	return -1;
}

/* Single binding token: Ctrl-X / Ctrl-Space / F1..F12 / names.
Printable single chars are rejected so a rebinding can never shadow
normal typing (only control, KEY_* and DEL codes are bindable). */
static int config_parse_binding(char *token, int *out)
{
	size_t len;

	if(token == NULL || out == NULL)
	{
		return -1;
	}

	token = config_trim(token);

	if(token[0] == '\0')
	{
		return -1;
	}

	config_lower(token);
	len = strlen(token);

	if(len > 5 && strncmp(token, "ctrl-", 5) == 0)
	{
		const char *rest;

		rest = token + 5;

		if(strcmp(rest, "space") == 0)
		{
			*out = CTRL_KEY(' ');
			return 0;
		}

		if(rest[0] != '\0' && rest[1] == '\0')
		{
			*out = CTRL_KEY(rest[0]);
			return 0;
		}

		return -1;
	}

	if(config_named_key(token, out) == 0)
	{
		return 0;
	}

	return -1;
}

static const ConfigDefault *config_default_for(const char *action)
{
	size_t i;

	if(action == NULL)
	{
		return NULL;
	}

	for(i = 0; i < sizeof(s_defaults) / sizeof(s_defaults[0]); i++)
	{
		if(strcmp(s_defaults[i].action, action) == 0)
		{
			return &s_defaults[i];
		}
	}

	return NULL;
}

static ConfigEntry *config_user_for(const char *action)
{
	int i;

	if(action == NULL)
	{
		return NULL;
	}

	for(i = 0; i < s_user_count; i++)
	{
		if(strcmp(s_user[i].action, action) == 0)
		{
			return &s_user[i];
		}
	}

	return NULL;
}

/* A user-claimed key survives only when it does not steal the default
of an action the user left untouched; swaps (both sides rebound)
always work. Unclaimed defaults below fill untouched actions. */
static int config_default_owned_elsewhere(const char *action, int key)
{
	size_t i;

	for(i = 0; i < sizeof(s_defaults) / sizeof(s_defaults[0]); i++)
	{
		int k;

		if(strcmp(s_defaults[i].action, action) == 0)
		{
			continue;
		}

		for(k = 0; k < s_defaults[i].nkeys; k++)
		{
			if(s_defaults[i].keys[k] != key)
			{
				continue;
			}

			if(config_user_for(s_defaults[i].action) == NULL)
			{
				return 1;
			}
		}
	}

	return 0;
}

static void config_store_user(const char *action, int *keys, int nkeys)
{
	ConfigEntry *slot;

	if(action == NULL || keys == NULL || nkeys <= 0)
	{
		return;
	}

	slot = config_user_for(action);

	if(slot == NULL)
	{
		if(s_user_count >= CONFIG_MAX_USER)
		{
			return;
		}

		slot = &s_user[s_user_count++];
		snprintf(slot->action, sizeof(slot->action), "%s", action);
	}

	slot->nkeys = 0;

	for(int i = 0; i < nkeys && slot->nkeys < CONFIG_MAX_KEYS_PER_ACTION; i++)
	{
		slot->keys[slot->nkeys++] = keys[i];
	}
}

static void config_parse_key_value(const char *action, char *value)
{
	int keys[CONFIG_MAX_KEYS_PER_ACTION];
	int nkeys;
	char *tok;
	int good;

	if(action == NULL || value == NULL)
	{
		return;
	}

	if(config_default_for(action) == NULL)
	{
		config_note("Config: unknown key");
		return;
	}

	nkeys = 0;
	good = 0;
	tok = strtok(value, ",");

	while(tok != NULL)
	{
		int code;

		if(config_parse_binding(tok, &code) == 0)
		{
			if(nkeys < CONFIG_MAX_KEYS_PER_ACTION)
			{
				keys[nkeys++] = code;
				good = 1;
			}
		}
		else
		{
			config_note("Config: bad binding ignored");
		}

		tok = strtok(NULL, ",");
	}

	if(good == 0)
	{
		return;
	}

	config_store_user(action, keys, nkeys);
}

static void config_apply_preset(const char *name)
{
	static const int def[CONFIG_HL_COUNT] = { 3, 2, 6, 5, 1, -1, -1 };
	static const int hi[CONFIG_HL_COUNT] = { 7, 3, 7, 6, 5, -1, -1 };
	static const int acid[CONFIG_HL_COUNT] = { 156, 108, 243, 223, 149, 250, 234 };
	int i;

	if(name == NULL)
	{
		return;
	}

	if(strcmp(name, "mono") == 0)
	{
		s_mono = 1;
		s_acid = 0;

		for(i = 0; i < CONFIG_HL_COUNT; i++)
		{
			s_fg[i] = -1;
			s_fg_set[i] = 0;
		}

		return;
	}

	s_mono = 0;
	s_acid = 0;

	if(strcmp(name, "high-contrast") == 0)
	{
		for(i = 0; i < CONFIG_HL_COUNT; i++)
		{
			s_fg[i] = hi[i];
			s_fg_set[i] = 0;
		}

		return;
	}

	if(strcmp(name, "acid") == 0)
	{
		s_acid = 1;

		for(i = 0; i < CONFIG_HL_COUNT; i++)
		{
			s_fg[i] = acid[i];
			s_fg_set[i] = 0;
		}

		return;
	}

	if(strcmp(name, "default") == 0)
	{
		for(i = 0; i < CONFIG_HL_COUNT; i++)
		{
			s_fg[i] = def[i];
			s_fg_set[i] = 0;
		}

		return;
	}

	config_note("Config: unknown theme ignored");
}

static void config_parse_line(char *line)
{
	char *eq;
	char *key;
	char *value;
	char action[32];
	long v;
	char *end;

	if(line == NULL)
	{
		return;
	}

	line = config_trim(line);

	if(line[0] == '\0' || line[0] == '#' || line[0] == ';')
	{
		return;
	}

	eq = strchr(line, '=');

	if(eq == NULL)
	{
		config_note("Config: bad line ignored");
		return;
	}

	*eq = '\0';
	key = config_trim(line);
	value = config_trim(eq + 1);
	config_lower(key);

	if(key[0] == '\0' || value[0] == '\0' || strlen(key) >= CONFIG_KEY_LIMIT)
	{
		config_note("Config: bad line ignored");
		return;
	}

	if(strcmp(key, "tab_width") == 0)
	{
		config_lower(value);
		v = strtol(value, &end, 10);

		if(end == value || *end != '\0' ||
			v < CONFIG_MIN_TAB_WIDTH || v > CONFIG_MAX_TAB_WIDTH)
		{
			config_note("Config: bad tab_width ignored");
			return;
		}

		s_tab_width = (int)v;
		return;
	}

	if(strcmp(key, "theme") == 0)
	{
		config_lower(value);
		config_apply_preset(value);
		return;
	}

	if(strncmp(key, "color.", 6) == 0)
	{
		int idx;
		int fg;

		idx = config_class_index(key + 6);

		if(idx < 0)
		{
			config_note("Config: unknown color ignored");
			return;
		}

		config_lower(value);

		if(config_parse_color(value, &fg) != 0)
		{
			config_note("Config: bad color ignored");
			return;
		}

		s_fg[idx] = fg;
		s_fg_set[idx] = 1;
		s_mono = 0;
		return;
	}

	if(strncmp(key, "key.", 4) == 0)
	{
		if(strlen(key + 4) >= sizeof(action))
		{
			config_note("Config: unknown key");
			return;
		}

		snprintf(action, sizeof(action), "%s", key + 4);
		config_parse_key_value(action, value);
		return;
	}

	config_note("Config: unknown key");
}

static void config_rebuild_keys(void)
{
	size_t i;
	int k;

	s_resolved_count = 0;

	for(i = 0; i < sizeof(s_defaults) / sizeof(s_defaults[0]); i++)
	{
		ConfigEntry *slot;
		ConfigEntry *user;

		if(s_resolved_count >= CONFIG_MAX_ACTIONS)
		{
			break;
		}

		slot = &s_resolved[s_resolved_count++];
		snprintf(slot->action, sizeof(slot->action), "%s", s_defaults[i].action);
		slot->nkeys = 0;
		user = config_user_for(slot->action);

		if(user != NULL)
		{
			for(k = 0; k < user->nkeys; k++)
			{
				if(config_default_owned_elsewhere(slot->action, user->keys[k]))
				{
					config_note("Config: binding in use, kept default");
					continue;
				}

				if(slot->nkeys < CONFIG_MAX_KEYS_PER_ACTION)
				{
					slot->keys[slot->nkeys++] = user->keys[k];
				}
			}

			if(slot->nkeys == 0)
			{
				for(k = 0; k < s_defaults[i].nkeys; k++)
				{
					if(slot->nkeys < CONFIG_MAX_KEYS_PER_ACTION)
					{
						slot->keys[slot->nkeys++] = s_defaults[i].keys[k];
					}
				}
			}

			continue;
		}

		for(k = 0; k < s_defaults[i].nkeys; k++)
		{
			if(slot->nkeys < CONFIG_MAX_KEYS_PER_ACTION)
			{
				slot->keys[slot->nkeys++] = s_defaults[i].keys[k];
			}
		}
	}
}

static int config_try_file(const char *path)
{
	FILE *fp;
	char line[CONFIG_LINE_LIMIT];

	if(path == NULL || path[0] == '\0')
	{
		return 0;
	}

	fp = fopen(path, "r");

	if(fp == NULL)
	{
		return 0;
	}

	while(fgets(line, sizeof(line), fp) != NULL)
	{
		if(strchr(line, '\n') == NULL && !feof(fp))
		{
			int c;

			config_note("Config: long line skipped");

			do
			{
				c = fgetc(fp);
			} while(c != '\n' && c != EOF);

			continue;
		}

		config_parse_line(line);
	}

	fclose(fp);
	return 1;
}

void config_load(void)
{
	const char *xdg;
	const char *home;
	char path[CONFIG_PATH_LIMIT];

	if(s_loaded != 0)
	{
		return;
	}

	s_loaded = 1;
	xdg = getenv("XDG_CONFIG_HOME");
	home = getenv("HOME");

	if(xdg != NULL && xdg[0] != '\0')
	{
		if(snprintf(path, sizeof(path), "%s/sulf/config", xdg) < (int)sizeof(path))
		{
			if(config_try_file(path) != 0)
			{
				config_rebuild_keys();
				return;
			}
		}
	}

	if(home == NULL || home[0] == '\0')
	{
		config_rebuild_keys();
		return;
	}

	if(snprintf(path, sizeof(path), "%s/.config/sulf/config", home) < (int)sizeof(path))
	{
		if(config_try_file(path) != 0)
		{
			config_rebuild_keys();
			return;
		}
	}

	if(snprintf(path, sizeof(path), "%s/.config/sulf", home) < (int)sizeof(path))
	{
		if(config_try_file(path) != 0)
		{
			config_rebuild_keys();
			return;
		}
	}

	config_rebuild_keys();
}

int config_tab_width(void)
{
	if(s_tab_width < CONFIG_MIN_TAB_WIDTH || s_tab_width > CONFIG_MAX_TAB_WIDTH)
	{
		return CONFIG_DEFAULT_TAB_WIDTH;
	}

	return s_tab_width;
}

int config_key_matches(const char *action, int key)
{
	int i;
	int k;

	if(action == NULL)
	{
		return 0;
	}

	if(s_loaded == 0)
	{
		const ConfigDefault *def;

		def = config_default_for(action);

		if(def == NULL)
		{
			return 0;
		}

		for(k = 0; k < def->nkeys; k++)
		{
			if(def->keys[k] == key)
			{
				return 1;
			}
		}

		return 0;
	}

	for(i = 0; i < s_resolved_count; i++)
	{
		if(strcmp(s_resolved[i].action, action) != 0)
		{
			continue;
		}

		for(k = 0; k < s_resolved[i].nkeys; k++)
		{
			if(s_resolved[i].keys[k] == key)
			{
				return 1;
			}
		}

		return 0;
	}

	return 0;
}

int config_highlight_fg(int cls)
{
	if(cls < 0 || cls >= CONFIG_HL_COUNT)
	{
		return -1;
	}

	return s_fg[cls];
}

int config_highlight_mono(void)
{
	int i;

	if(s_mono == 0)
	{
		return 0;
	}

	for(i = 0; i < CONFIG_HL_COUNT; i++)
	{
		if(s_fg_set[i] != 0)
		{
			return 0;
		}
	}

	return 1;
}

int config_highlight_acid(void)
{
	return s_acid;
}

const char *config_notice(void)
{
	return s_notice;
}
