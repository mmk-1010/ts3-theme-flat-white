# Fluent Foundation for TeamSpeak 3

The theme and plugin share neutral gray color roles, type sizes, corners, spacing, and the motion duration in `tokens.json`. The palette reserves color for semantic statuses such as talking, away, and mute. The design follows the [Fluent‑Qt Foundation documentation](https://calvinhxx.github.io/Fluent-Qt/api/foundation/).

Color pairs are ordered Flat White, then Flat Dark. Edit `widgets.qss.in` for Qt Widgets rules, `richtext.qss.in` for Qt rich text, and the tooltip templates for the channel tree. TeamSpeak reads the generated `QPalette` comments and resolves `styles:` URLs at runtime. Keep its selectors, control IDs, and `%%…%%` placeholders intact.

From the repo root, run `python foundation/generate.py` after changing the tokens or templates. The generator writes both themes into `src/styles/` and generates `plugin/src/foundation_tokens.h`. Use `python foundation/generate.py --check` to catch stale output, missing theme resources, and text contrast regressions. Keep the generated files with the source changes so TeamSpeak and the MSVC build can consume them directly.
