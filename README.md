# sulf

The text editor that stinks.

## Build

Requires `libncurses-dev` (wide build) and a C11 compiler:

```
make          # builds build/sulf, zero warnings
make clean    # wipes build/
```

Run as `build/sulf [file ...]`. Each file opens in its own tab
(top row: click a tab or use `Ctrl-]` / `Ctrl-^` to switch).

## Keys

| Keys | Action |
| ---- | ------ |
| Arrows / Home / End / PgUp / PgDn | Move (`Ctrl-A` / `Ctrl-E` also Home / End) |
| Shift+Arrows (and friends) | Extend selection |
| Type / Enter / Backspace / Del | Edit (`Tab` indents, `Shift-Tab` unindents) |
| `Ctrl-S` | Save (prompts for a name if unnamed) |
| `Ctrl-Q` | Quit (prompts per unsaved buffer) |
| `Ctrl-]` / `Ctrl-^` | Next / previous buffer tab |
| `Ctrl-C` / `Ctrl-X` / `Ctrl-V` | Copy / cut / paste selection |
| `Ctrl-F`, then `n` / `N` / `Enter` | Find next / previous match, `Esc` cancels |
| `Ctrl-P` / `F1` | Command palette, `Esc` cancels |
| `F2` | File explorer (`Right` expand, `Enter` open, `Left` collapse, `Esc` cancels) |
| `Ctrl-Z` / `Ctrl-Y` | Undo / redo (per buffer) |
| `Ctrl-Space` | Force completion (`Tab` / `Enter` accept) |
| Mouse click / wheel | Move cursor / scroll / switch tab |

`--help` prints the same list, `--version` prints the version.

## Config

Optional file at `$XDG_CONFIG_HOME/sulf/config`, else
`~/.config/sulf/config` (legacy `~/.config/sulf` also works).
Missing file keeps defaults; bad lines are ignored with a warning.
Lines are `key = value` (`#` comments, last-wins):

```
tab_width = 4            # 1..16, display tab stops + space-unindent width
theme = acid             # default | acid | mono | high-contrast
color.keyword = yellow   # keyword/string/comment/number/preproc:
color.string = green     # black red green yellow blue magenta cyan
color.comment = cyan     # white default none, or a 0-255 palette
                         # index when the terminal supports 256 colors
color.text = 250         # base text fg (soft white, acid only)
color.background = 234   # base background (dark, acid only)
key.save = Ctrl-S        # Ctrl-<letter|Space|]|^>  F1..F12
key.palette = Ctrl-P,F1  # Tab Enter Backspace Delete DEL
                         # Up Down Left Right Home End PgUp PgDn
                         # Shift-Tab Shift-Left/Right/Up/Down/Home/End/PgUp/PgDn
```

Every main keybinding is rebindable (`quit save undo redo copy cut
paste find palette explorer next-tab prev-tab left right up down
sel-left sel-right sel-up sel-down page-up page-down sel-page-up
sel-page-down home end sel-home sel-end unindent backspace delete
newline force-complete tab`). Overriding an action replaces its whole
list; a rebinding that steals another action's default is rejected
unless that action is rebound too (so swaps work). Printable characters
can't be bound, so typing is never shadowed. `--help` and the status
line always show the default keys.

## Notes

- Tabs are stored as tab bytes and spaces as spaces — neither is ever
  converted (tab stops render every `tab_width` columns, default 4,
  display only).
  Line endings round-trip: LF, CRLF, and mixed files keep their style on
  save (mixed normalizes to LF after saving).
- Full UTF-8: cursor, editing, search, and display are character-aware;
  invalid bytes degrade to single cells instead of corrupting text.
  Completion matches ASCII words only. Needs a UTF-8 locale (falls back to
  `C.UTF-8` / `C.utf8` when the environment does not set one) and an
  xterm-compatible terminal for mouse and focus tracking.
- Saves are atomic (write temp + rename), preserve file mode, and never
  truncate the original on failure. Losing terminal focus auto-saves every
  named buffer with unsaved changes.
- Undo keeps up to 64 full-buffer snapshots per buffer; typing runs coalesce so one
  `Ctrl-Z` reverts one typed stretch. Selection edits, indent, completion,
  and palette actions are all undoable.
- Syntax highlighting covers C (`.c`/`.h`) and Markdown (`.md`).

## Layout

`src/` + `include/` (see `AGENTS.md` for the contributor contract),
`Makefile` builds `build/sulf`. `TODO.md` tracks future ideas.

## License

MIT — see `LICENSE`.
