# sulf

The text editor that stinks.

## Build

Requires `libncurses-dev` (wide build) and a C11 compiler:

```
make          # builds build/sulf, zero warnings
make clean    # wipes build/
```

Run as `build/sulf [file ...]`. Each file opens in its own tab
(top row: click a tab or use `Ctrl-]` / `Ctrl-^` to switch, up to 16 tabs).
Unknown `-opt` flags are rejected. `--help` lists keys, `--version` prints the version.

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
| `F2` | File explorer (`Right` expand, `Enter` open, `Left` collapse, `Esc` closes, `*` wildcard in filter) |
| `Ctrl-Z` / `Ctrl-Y` | Undo / redo (per buffer) |
| `Ctrl-Space` | Force completion (`Tab` / `Enter` accept) |
| Mouse click / wheel | Move cursor / scroll / switch tab (click the minimap to jump) |

Closing a tab refuses dirty buffers. Closing the last tab resets to a fresh unnamed buffer.

## Tabs, palette, explorer

Tabs live in the top row. Switching tabs keeps one shared cursor and clears
selection. Opening a file reuses the pristine unnamed tab when possible,
otherwise it opens or switches to that file's tab.

The palette (`Ctrl-P` / `F1`) lists File, Search, Edit, and Buffer commands.
Type to fuzzy filter by name, or by hidden `/cmd` alias (for example `/save-as`).
It shows your live `key.*` bindings on the right.

The explorer (`F2`) is fullscreen and starts at the folder where you launched
`sulf`. It cannot go above that folder. Dotfiles are always shown. Typing
filters across subfolders by name or path. A `*` in the query switches to
glob matching. `Right` expands a folder in tree mode, `Enter` opens a file
or folder, `Left` collapses, `Backspace` edits the query.

## Finding and completing

Find (`Ctrl-F`) is sticky and case sensitive. Plain moves, clicks, and
completion keep the query. Edits clear the highlight but keep the query
for the next `n` / `N`.

Completion pops up after 2 word characters (ASCII letters, digits, `_`),
or on `Ctrl-Space`. `Tab` / `Enter` accepts and adds one trailing space.
`Esc` cancels.

## Screen

The left gutter shows absolute line numbers. Past the end of the file the
gutter shows a bare line marker.

The right edge shows an always visible minimap with a wash for the
viewport and an underline for the cursor row. Its tint follows the
dominant highlight on that row.

If the file is inside a git repo, the gutter marks lines added, modified,
or deleted versus `HEAD`. Markers stay silent for unnamed files, files
outside a repo, and when git is missing.

The bottom line shows status messages, the cursor position, the file type,
and whether the buffer is modified.

## Config

Optional file at `$XDG_CONFIG_HOME/sulf/config`, else
`~/.config/sulf/config` (legacy `~/.config/sulf` also works).
Missing file keeps defaults; bad lines are ignored with a warning.
Lines are `key = value` (`#` or `;` comments, last-wins):

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
can't be bound, so typing is never shadowed. `--help` always shows the
default keys; the palette shows your live keys.

## Notes

- Tabs are stored as tab bytes and spaces as spaces, neither is ever
  converted (tab stops render every `tab_width` columns, default 4,
  display only). `Tab` indents the selection, `Shift-Tab` unindents.
  Typing, `Enter`, `Backspace`, and `Del` replace the selection.
  Line endings round-trip: LF, CRLF, and mixed files keep their style on
  save (mixed normalizes to LF after saving).
- Full UTF-8: cursor, editing, search, and display are character-aware;
  invalid bytes degrade to single cells instead of corrupting text.
  Completion matches ASCII words only. Needs a UTF-8 locale (falls back to
  `C.UTF-8` / `C.utf8` when the environment does not set one) and an
  xterm-compatible terminal for mouse and focus tracking.
- Saves are atomic (write temp + rename), preserve file mode, and never
  truncate the original on failure. Losing terminal focus auto-saves every
  named buffer with unsaved changes. Files changed by another program
  reload automatically when the buffer has no unsaved changes (checked on
  focus, tab switch, and every frame); unsaved buffers keep their edits
  with a warning, and deleted files report instead of reloading.
- Undo keeps up to 64 full-buffer snapshots per buffer; typing runs coalesce so one
  `Ctrl-Z` reverts one typed stretch. Selection edits, indent, completion,
  and palette actions are all undoable.
- Copy needs a selection. The clipboard holds up to 8KB and warns on
  truncation instead of cutting silently. Paste replaces the selection
  and lands the cursor at the end of the insert.
- Syntax highlighting covers C (`.c`/`.h`), Markdown (`.md`), JavaScript
  (`.js`/`.cjs`), PHP (`.php`), C# (`.cs`), SQL (`.sql`), and YAML
  (`.yaml`/`.yml`).

## Layout

`src/` + `include/` (see `AGENTS.md` for the contributor contract),
`Makefile` builds `build/sulf`. `TODO.md` tracks future ideas.

## License

MIT, see `LICENSE`.
