# AGENTS.md

C11 ncurses text editor. Single binary, no test framework, no CI.

## Build

- `make` → `build/editor`. `make clean` wipes `build/` (gitignored — never commit it).
- Requires `libncurses-dev`; link flag is `-lncurses` (`Makefile` `LDLIBS`).
- `make` must be zero-warning (`-Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L`).
- New `.c` file: add to `Makefile` `OBJS` **and** add a per-object header-dep line, else incremental builds go stale.

## Layout (`src/` + `include/`, global `Editor g_editor` in `editor.h`)

- `main.c`: `editor_init → editor_open → editor_start_screen → editor_run → editor_free`.
- `buffer.c`: open/save, EOL detection/preservation. `edit.c`: mutations + cursor moves. `view.c`: scroll/gutter/draw. `input.c`: keys + prompts. `screen.c`: ncurses/termios init. `complete.c` / `highlight.c` / `mouse.c`: features.
- Style (enforced): tabs, Allman braces, guard clauses, `NULL` uppercase.

## Invariants agents commonly break

- Tabs are stored as spaces (`TAB_WIDTH 4`); byte index == screen cell.
- EOL: `EolKind` + `trailing_nl` round-trip on save (`MIXED` saves as LF).
- Cursor: `want_cx` goal column; roam below EOF is bounded by `editor_max_cy()` (`num_lines + view_rows - 2`), typing there is ignored via `NULL` guards, `col_off` is preserved on virtual rows.
- Past-EOF rows draw a bare `ACS_VLINE` separator; gutter numbers are absolute, min width 2.
- Quit: `Ctrl-Q`/`Ctrl-X` prompt `y`/`n`/`Esc`; `Ctrl-S` saves (prompts filename if unnamed).
- `screen.c` disables termios `IXON`/`IXOFF` so `Ctrl-S`/`Ctrl-Q` reach the app — don't remove.

## Feature contracts

- Complete: auto-popup after 2 word chars + `Ctrl-Space` force; `Tab`/`Enter` accept, `Esc` cancels; case-sensitive, frequency then A-Z; fixed tables, no malloc.
- Highlight: C files only (`.c`/`.h`), default-background palette, per-line cache + full rescan on dirty flag.
- Mouse: click-to-move + wheel scroll (±3 rows); silent-off fallback when terminal lacks support.

## Verification (headless pty, no fixtures)

- Run with `TERM=xterm` under `pty.fork()`; drive keys, then `Ctrl-Q` (+`n` if dirty) to exit.
- Mouse input must be SGR-1006: `\x1b[<btn;x;yM`, 1-based (`0`=press, `64`/`65`=wheel).
- ncurses sends delta frames (`\x1b[nS`/`\x1b[nT` scrolls, single-cell status updates): assert on **single-frame drains** or **last match** of `\x1b[r;cH` / `\x1b[nG` — never byte counts or full-status regexes over accumulated output.
- Keep throwaway verify scripts in `/tmp`, not the repo.
