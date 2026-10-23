# Changelog

## 1.0.0

Initial release.

- Modal-free editing: type, selection, block indent/unindent, join/split lines
- Atomic save (temp + rename) with EOL round-trip (LF/CRLF/mixed) and mode preservation
- Full UTF-8: character-aware cursor, editing, search, and display (wide ncurses)
- Undo/redo (64 snapshots, coalesced typing) covering edits, selection, and palette actions
- Find with match navigation, command palette, word completion
- C and Markdown syntax highlighting, minimap, mouse support, focus auto-save
- `--help` / `--version`, README, MIT license
