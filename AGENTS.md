# AGENTS.md

C11 ncurses editor. Single binary, no tests (`tests/` empty), no CI.
`TODO.md` is ideas-only — never implement from it. `Makefile` is truth, README is user docs.






## Build

- `make` → `build/sulf` (zero warnings: `-Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L`). `make clean` wipes `build/` (gitignored, never commit).
- Requires `libncurses-dev`; link `-lncursesw` (UTF-8 needs wide build).
- Run `build/sulf [file ...]` (one tab per file). `--help`/`-h`, `--version`/`-V`; unknown `-opt` errors out.
- New `.c` file: add to `Makefile` `OBJS` **and** a per-object header-dep line, or incremental builds go stale.

## Layout

- `src/` + `include/`. `Buffer` (content + per-buffer undo/hl/search/complete/git) + `View` (single shared cursor/scroll/selection) in `editor.h`; `tabs.c` owns fixed `MAX_BUFFERS 16` table, no malloc.
- Flow: `main.c`: `tabs_init → config_load → tabs_open_file` per CLI file (re-activate first) `→ editor_start_screen` (+ `config_notice` to status) `→ editor_run` (`input.c`) `→ editor_end_screen → tabs_free`.
- `view.c`: scroll/draw; row 0 tab strip, text rows `1..LINES-3` (`VIEW_TOP 1`, `view_rows = LINES-3`). `screen.c`: locale/termios/ncurses init. `config.c`: static, no malloc.
- Style: tabs, Allman braces, guard clauses, `NULL` uppercase.

## Config (`config.c` consumed by `input.c`)

- Paths in order, first existing wins: `$XDG_CONFIG_HOME/sulf/config`, `~/.config/sulf/config`, legacy `~/.config/sulf`. Missing keeps defaults; bad lines ignored with status notice.
- `tab_width` 1..16, default 4 — display-only, never converts bytes.
- `key.<action>` rebinds main keys (defaults in `config.c` mirror `input.c`). Printable chars rejected (typing never shadowed). Stealing another action's default rejected unless that action is also rebound (swaps work). Search/palette/explorer/completion modal keys stay fixed.

## Invariants agents break

- Buffer is bytes, not cells: `cx`/`want_cx`/`col_off`/selection are byte indices snapped to char starts (`editor_clamp_cursor` + char motions/deletes). Screen columns come from `line.c` tab-aware width helpers, never byte counts. Tabs stay literal `\t`; unindent strips one `\t` or up to `tab_width` spaces. Completion words ASCII-only.
- EOL round-trips (`EolKind` + `trailing_nl`; `MIXED` saves as LF then becomes LF). Saves are atomic (temp + rename), preserve mode, never truncate on failure.
- Cursor may roam below EOF bounded by `editor_max_cy()` (`num_lines + view_rows - 2`); typing there is ignored via `NULL` guards, `col_off` preserved. Past-EOF rows draw bare `ACS_VLINE`; gutter numbers absolute, min width 2.
- Tabs share one `View`: switches don't carry cursor/selection/scroll; switch clears selection, resets completion, deactivates search (query kept). Per-buffer undo/hl/search/complete state dies with the tab. Explorer `Enter` / palette Open reuses pristine current tab, else open-or-switch. Close refuses dirty tabs; closing last resets to fresh unnamed. `Ctrl-Q` prompts `y`/`n`/`Esc` per dirty buffer (`Ctrl-X` is cut, not quit); `Ctrl-S` saves (Save-as prompts if unnamed).
- Keys: no `KEY_SUP`/`KEY_SDOWN` — Shift+Up/Down are `KEY_SR`/`KEY_SF` (`kri`/`kind`); Shift+arrows/Home/End `KEY_SLEFT`/`SRIGHT`/`SHOME`/`SSEND`, Shift+PgUp/PgDn `KEY_SPREVIOUS`/`KEY_SNEXT`; `Ctrl-A`/`Ctrl-E` also Home/End. `Ctrl-]`/`Ctrl-^` switch tabs. Shift+Tab (`KEY_BTAB`) unindents; no Ctrl+Tab binding (xterm sends bare `0x09`).
- `screen.c` disables `IXON`/`IXOFF` so `Ctrl-S`/`Ctrl-Q` reach the app — don't remove. `raw()` passes `Ctrl-C` through, so it is safe to bind. Locale `""` fallback `C.UTF-8`/`C.utf8`. Never call bare `endwin()` (`main.c`, `die()`); always `editor_end_screen()` (focus shutdown then `endwin`) or `[O` sprays the shell.
- Focus: xterm `ESC[?1004h`/`l` + `define_key("\033[I"/"\033[O", 1001/1002)`; never parse `ESC O` manually. Focus-out saves only dirty+named (`Auto-saved`, failures status-only).
- Undo: per-buffer 64 full snapshots; any new edit clears redo; consecutive char insert/backspace coalesce via `undo_begin`/`end`; unindent pushes only if stripped. Free target slot before capture; on eviction NULL the memmove-duplicated top slot first (it aliases the shifted entry).
- Git: `git diff -U0 HEAD -- file` → capped 512-hunk table; over cap disables markers. Refresh only on open/save/tab-switch/focus-in via epoch clock — never in draw path. Only `git_marker()` runs in draw (pure lookup, tints gutter `ACS_VLINE`). Silent-off for unnamed/untracked/non-repo/missing-git; `git -C dir`, never changes cwd.

## Feature contracts (one line each)

- Highlight: C (`.c`/`.h`), Markdown (`.md`), JavaScript (`.js`/`.cjs`), PHP (`.php`), C# (`.cs`), SQL (`.sql`), YAML (`.yaml`/`.yml`); per-line cache + full rescan on dirty; tracks `/* */` and ```` ``` ````/`~~~` state.
- Complete: popup after 2 word chars + `Ctrl-Space`; `Tab`/`Enter` accept, `Esc` cancels; accept clears selection, deactivates search, appends one trailing space.
- Search: `Ctrl-F` sticky, `strstr`, case-sensitive, 512 matches/buffer; `n`/`Enter` next, `N` prev, `Esc` cancels. Edits/clicks/completion-accept deactivate (query kept); nav/wheel/resize preserve.
- Select: end-exclusive, anchor `sel_ax`/`ay` + active `cx`/`cy`. Only plain moves, mouse clicks, completion-accept clear. Typing/`Enter`/Backspace/Del replace/delete range; `Tab` block-indents.
- Palette: `Ctrl-P`/`F1`, subsequence filter on `/cmd - name`, fixed 20-entry table, 40-col modal; `Enter` snapshots id then resets before sub-prompts; Quit propagates `editor_request_quit()`. Open/run clears completion, deactivates search.
- Explorer: exclusive fullscreen view (never overlays editor). `F2` toggles at file's dir (`.` if unnamed), `F2`/`Esc` closes. Filter deep-searches subfolders (name-or-path fuzzy), showing hits with ancestor tree paths (tree art preserved, `../` excluded); `Right` expands in tree mode else cds/opens, `Left` collapses in tree mode else no-op, `Enter` cds/opens via `tabs_open_file`, `Backspace` edits query. No `../` row at filesystem root under any spelling (`.`/`..` chains included — detect via `stat` identity, never bare `strcmp(cwd, "/")`). Dotfiles always shown, no hidden toggle.
- Clipboard: static 8KB, no malloc. Copy needs non-empty selection; cut = copy + one `UNDO_OTHER` delete; paste replaces selection, splits/joins lines, lands cursor at end of insert, no-op on virtual rows. Truncation reports, never silent. Cut/paste deactivate search.
- Mouse: click-to-move + wheel ±3 rows + map-column jump + tab-bar click; silent-off without terminal support. Never touches `sel_active` (no drag/release). Clicks clear selection, reset completion, deactivate search.
- Map: always-visible right edge (3 cells + `ACS_VLINE`, 0 if no text col left). Tint = dominant highlight class + `A_REVERSE`; viewport wash + cursor-bucket underline.
- Page: `PgUp`/`PgDn` move exactly `LINES-3` rows; `row_off` follows via per-frame `editor_scroll()`, not inside the move.
- Enter inherits the split line's leading spaces/tabs (capped at `cx`).
