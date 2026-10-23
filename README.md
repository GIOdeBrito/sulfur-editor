# sulf

A small terminal text editor. Single binary, no dependencies beyond ncurses.

## Build

Requires `libncurses-dev` (wide build) and a C11 compiler:

```
make          # builds build/sulf, zero warnings
make clean    # wipes build/
```

Run as `build/sulf [filename]`.

## Keys

| Keys | Action |
| ---- | ------ |
| Arrows / Home / End / PgUp / PgDn | Move (`Ctrl-A` / `Ctrl-E` also Home / End) |
| Shift+Arrows (and friends) | Extend selection |
| Type / Enter / Backspace / Del | Edit (`Tab` indents, `Shift-Tab` unindents) |
| `Ctrl-S` | Save (prompts for a name if unnamed) |
| `Ctrl-Q` | Quit (prompts if unsaved changes) |
| `Ctrl-C` / `Ctrl-X` / `Ctrl-V` | Copy / cut / paste selection |
| `Ctrl-F`, then `n` / `N` / `Enter` | Find next / previous match, `Esc` cancels |
| `Ctrl-P` / `F1` | Command palette, `Esc` cancels |
| `F2` | File explorer, `Esc` cancels |
| `Ctrl-Z` / `Ctrl-Y` | Undo / redo |
| `Ctrl-Space` | Force completion (`Tab` / `Enter` accept) |
| Mouse click / wheel | Move cursor / scroll |

`--help` prints the same list, `--version` prints the version.

## Notes

- Tabs are stored as tab bytes and spaces as spaces — neither is ever
  converted (tab stops render at every 4th column, display only).
  Line endings round-trip: LF, CRLF, and mixed files keep their style on
  save (mixed normalizes to LF after saving).
- Full UTF-8: cursor, editing, search, and display are character-aware;
  invalid bytes degrade to single cells instead of corrupting text.
  Completion matches ASCII words only. Needs a UTF-8 locale (falls back to
  `C.UTF-8` / `C.utf8` when the environment does not set one) and an
  xterm-compatible terminal for mouse and focus tracking.
- Saves are atomic (write temp + rename), preserve file mode, and never
  truncate the original on failure. Losing terminal focus auto-saves named
  buffers with unsaved changes.
- Undo keeps up to 64 full-buffer snapshots; typing runs coalesce so one
  `Ctrl-Z` reverts one typed stretch. Selection edits, indent, completion,
  and palette actions are all undoable.
- Syntax highlighting covers C (`.c`/`.h`) and Markdown (`.md`).

## Layout

`src/` + `include/` (see `AGENTS.md` for the contributor contract),
`Makefile` builds `build/sulf`. `TODO.md` tracks future ideas.

## License

MIT — see `LICENSE`.
