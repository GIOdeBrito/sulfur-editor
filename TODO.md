# TODO

Ideas for future work, roughly ordered by value/effort.
Ideas only — never implement unasked.

## Editing
- Bracket/quote auto-close with skip-over on the closing char
- Smart Backspace: one press removes a full indent level when the line is whitespace-only
- `o`/`O`-style open-line below/above (insert blank indented line without splitting)
- Join lines (`Ctrl-J`): append next line with a single space, trim extra whitespace
- Duplicate line / move line up/down
- Comment toggle (`Ctrl-/`): prefix touched lines with `//`, keep selection

## Navigation & search
- Find & replace with `y/n/a` confirm per match
- Go to line (`Ctrl-G`): prompt for a number, clamp like page moves
- Matching-bracket jump (`%` or `Ctrl-]`) using the C highlighter state
- Go-to-definition / back via ctags file (no LSP yet)
- Code folding (per-indent or brace-based)

## Status & chrome
- Status enrichment: `cx`/col, encoding, branch, diag count
- Relative/hybrid line numbers toggle

## Quality of life
- Word wrap toggle (soft wrap, no buffer change)
- Periodic backup file (`.#name` + restore prompt)
- Split view: multiple panes per tab, one buffer per pane (tabs done)
- Config file (`~/.config/sulf`: keys, theme, `TAB_WIDTH`)

## Platform / IDE growth
- 256-color / truecolor palette detection, theme option
- Clipboard integration: OSC-52 yank, bracketed paste
- Wider highlight coverage: `.py`, `.js`, `.sh` keyword sets
- Man page (README covers 1.0 docs)
- Basic unit tests for buffer/edit ops (currently verified via headless pty only)
- Async job control (lint/build/run in background)
- LSP stdio integration (diagnostics/completion)
- Git diff/blame gutter markers
- Incremental highlight (replace full rescan on dirty)
