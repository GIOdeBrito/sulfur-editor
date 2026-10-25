#ifndef CONFIG_H
#define CONFIG_H

#define CONFIG_DEFAULT_TAB_WIDTH 4
#define CONFIG_MIN_TAB_WIDTH 1
#define CONFIG_MAX_TAB_WIDTH 16
#define CONFIG_MAX_KEYS_PER_ACTION 3

/* Class indexes for config_highlight_fg(), in HlClass order subset. */
#define CONFIG_HL_KEYWORD 0
#define CONFIG_HL_STRING 1
#define CONFIG_HL_COMMENT 2
#define CONFIG_HL_NUMBER 3
#define CONFIG_HL_PREPROC 4
#define CONFIG_HL_COUNT 5

/* Load ~/.config/sulf (XDG-aware, see config.c). Missing file keeps
all defaults silently; bad lines are ignored with a status notice. */
void config_load(void);
/* Display tab-stop width, clamped to 1..16. Default 4. */
int config_tab_width(void);
/* Non-zero when key is bound to action (full input.c table). */
int config_key_matches(const char *action, int key);
/* Foreground for a highlight class: 0..7 ncurses color, -1 default. */
int config_highlight_fg(int cls);
/* Non-zero when theme "mono" left every class unset (plain text). */
int config_highlight_mono(void);
/* Short warning about ignored lines, or "" when nothing to report. */
const char *config_notice(void);

#endif
