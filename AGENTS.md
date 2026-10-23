# AGENTS.md

C11 ncurses text editor. Single binary, no test framework, no CI.
`TODO.md` is ideas-only — never implement from it unasked. `Makefile` is the source of truth (no README / opencode.json).

## Build

- `make` → `build/sulf`. `make clean` wipes `build/` (gitignored — never commit it).
- Run as `build/sulf [filename]` (`main.c` rejects `argc > 2`).
- Requires `libncurses-dev`; link flag is `-lncurses` (`Makefile` `LDLIBS`).
- `make` must be zero-warning (`-Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L`).
- New `.c` file: add to `Makefile` `OBJS` **and** a per-object header-dep line, else incremental builds go stale.

## Layout (`src/` + `include/`, global `Editor g_editor` in `editor.h`)

- `main.c`: `editor_init → editor_open → editor_start_screen → editor_run → editor_free`.
- `editor.c`: init/free, `editor_clamp_cursor`, selection helpers. `line.c`/`util.c`: line-buffer growth + `die`. `buffer.c`: open/save, EOL detection/preservation. `edit.c`: mutations + cursor moves. `view.c`: scroll/gutter/draw. `input.c`: keys + prompts + `editor_run` loop (clamp + `editor_scroll` + draw per frame). `screen.c`: ncurses/termios init. `complete.c` / `highlight.c` / `mouse.c` / `undo.c`: features.
- Style (enforced): tabs, Allman braces, guard clauses, `NULL` uppercase.

## Invariants agents commonly break

- Tabs are stored as spaces (`TAB_WIDTH 4`); byte index == screen cell.
- EOL: `EolKind` + `trailing_nl` round-trip on save (`MIXED` saves as LF, then resets to LF).
- Cursor: `want_cx` goal column; roam below EOF is bounded by `editor_max_cy()` (`num_lines + view_rows - 2`), typing there is ignored via `NULL` guards, `col_off` is preserved on virtual rows.
- Enter: new line inherits the split head line's leading spaces (capped at `cx`); cursor lands after the indent.
- Past-EOF rows draw a bare `ACS_VLINE` separator; gutter numbers are absolute, min width 2.
- Quit: `Ctrl-Q`/`Ctrl-X` prompt `y`/`n`/`Esc`; `Ctrl-S` saves (prompts filename if unnamed).
- `screen.c` disables termios `IXON`/`IXOFF` so `Ctrl-S`/`Ctrl-Q` reach the app — don't remove.

## Feature contracts

- Complete (`COMPLETE_MIN_PREFIX 2`, fixed 32×64 tables, no malloc): auto-popup after 2 word chars + `Ctrl-Space` force; `Tab`/`Enter` accept, `Esc` cancels; case-sensitive, frequency then `strcmp` A-Z. Accept clears selection and deactivates search.
- Highlight: C (`.c`/`.h`) + Markdown (`.md`), default-background palette, per-line cache + full rescan on dirty flag. C tracks `/* */` state, MD tracks ```` ``` ````/`~~~` fence state; MD reuses the C palette (headings keyword, code string, bold number, italic preproc, quote comment, lists/rules preproc, link text keyword + URL string).
- Mouse: click-to-move on text + wheel scroll (±3 rows, roams below EOF like Arrow Down bounded by `editor_max_cy()`) + map-column jump (empty buckets ignored); silent-off fallback when terminal lacks support. Never touches `sel_active` (no drag/release: `REPORT_MOUSE_POSITION`/`BUTTON1_RELEASED` off). Text clicks clear selection, reset completion, deactivate search (query kept).
- Map: always-visible right edge (`editor_map_width()` = 4: 3 content + `ACS_VLINE`; 0 when it would leave <1 text col, `view_cols` already narrowed). Buckets via `editor_map_bucket_start()` — 1:1 top-packed when the file fits, else floor-proportional. Per-column coverage of the longest line in the bucket (each cell samples one horizontal third of `view_cols`; last cell also covers overflow past `view_cols`). Tint is dominant non-`NORMAL` class of that line via `highlight_attr | A_REVERSE` (plain when unhighlighted/colorless). Viewport wash is `A_REVERSE` (`+A_BOLD` when covered); cursor bucket adds `A_UNDERLINE`.
- Select: `sel_active`/`sel_ax`/`sel_ay` anchor, active end is `cx`/`cy`, end-exclusive. Backspace/Del delete the range (multi-line joins head+tail, never drops below 1 line); typing/`Enter` replace it; `Tab` block-indents touched lines (`ex==0` excludes the last line) and shifts the selection. Only plain moves, mouse clicks, and completion-accept clear; both draw paths split runs with `A_REVERSE`.
- Search (`src/search.c`, static state, fixed 128-char query + 512-match table, no malloc): `Ctrl-F` sticky find; `n`/`Enter` next, `N` prev (wrap), `Esc` cancels, empty query reuses last; case-sensitive `strstr`. Nav keys/mouse-wheel/resize preserve; any edit/mouse-click/completion-accept deactivates (query kept). Other matches `A_BOLD`, current `A_REVERSE` in both draw paths via `search_cover()` (selection wins ties); jumps set `cx`/`cy`+`want_cx` and rely on per-frame `editor_scroll()`; no undo interaction.
- Palette (`src/palette.c`, static state, fixed 64-char query + 16-entry table, no malloc): `Ctrl-P`/`F1` opener (`F1` survives muxes claiming `Ctrl-P`; both share one branch after the completion block). Case-insensitive subsequence filter, stable table order; `Up`/`Ctrl-P` + `Down`/`Ctrl-N` wrap, `Enter` runs, `Esc` cancels. Dispatches only existing actions (save/save-as/quit/find family/undo/redo/indent/delete-selection/unindent/clear-selection/force-complete). `Enter` snapshots the id then resets before dispatch (getnstr sub-prompts take over input); Quit propagates `editor_request_quit()` as the return. Active branch sits after focus, before search — mouse clicks dismiss-then-fall-through, resize keeps state; open/run reset completion and deactivate search (query kept). Fixed 40-col centered modal at row 1 drawn after `complete_draw()` (`A_REVERSE` selection; long queries truncate on display only). Background dims via `palette_dim_background()` post-pass (`mvwinch` + `A_DIM` on every cell outside `palette_box()` — no `view.c` changes); `editor_run` parks the cursor in the query row. No undo interaction.
- Focus (`src/focus.c`, static, no malloc): xterm tracking via raw `ESC[?1004h`/`l` (no terminfo cap exists) + `define_key("\033[I"/"\033[O", 1001/1002)` so ncurses decodes — never parse `ESC [ O` manually or `[O` leaks in as text. Focus-out saves only when dirty+named (`Auto-saved`, failures status-only, dirty kept); unnamed buffers skipped. `editor_end_screen()` (shutdown-then-`endwin`) replaces both bare `endwin()` calls (`main.c`, `die()`) or `[O` sprays into the shell after quit.
- Keys: ncurses has **no** `KEY_SUP`/`KEY_SDOWN` — Shift+Up/Down arrive via `kri`/`kind` as `KEY_SR`/`KEY_SF`; Shift+arrows/Home/End are `KEY_SLEFT`/`SRIGHT`/`SHOME`/`SSEND`, Shift+PgUp/PgDn are `KEY_SPREVIOUS`/`KEY_SNEXT` (`Ctrl-A`/`Ctrl-E` also Home/End). Shift+Tab is `KEY_BTAB` → unindent (block over selection, else current line); there is deliberately **no** Ctrl+Tab binding (plain xterm sends bare `0x09`).
- Page: `KEY_PPAGE`/`KEY_NPAGE` move exactly `LINES-2` rows, clamped to `editor_max_cy()`; `row_off` follows via per-frame `editor_scroll()` in `editor_run` (not inside `editor_move_page`).
- Undo: `Ctrl-Z`/`Ctrl-Y`; full-buffer snapshots (content + cursor + selection + `dirty`) capped at 64, any new edit clears redo; consecutive char inserts/backspaces coalesce via `undo_begin`/`undo_end`; unindent pushes only when it actually strips.

## Verification (headless pty, no fixtures)

- Run with `TERM=xterm` under `pty.fork()`; drive keys, then `Ctrl-Q` (+`n` if dirty) to exit.
- Mouse input must be SGR-1006: `\x1b[<btn;x;yM`, 1-based (`0`=press, `64`/`65`=wheel); release is lowercase-`m`-terminated (`\x1b[<3;x;ym`).
- ncurses sends delta frames (`\x1b[nS`/`\x1b[nT` scrolls, single-cell status updates): assert on **single-frame drains** or **last match** of `\x1b[r;cH` / `\x1b[nG` — never byte counts or full-status regexes over accumulated output.
- Keep throwaway verify scripts in `/tmp`, not the repo.
