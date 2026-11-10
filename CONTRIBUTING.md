# Contributing

Small, focused changes are easiest to take. If you change behaviour, add or adjust a check in the
same pull request - the test programs are the reason the injection code can be changed at all.

## The loop

```
build.cmd            main program, both helpers, all test programs
build.cmd probe      main program without the elevation manifest, for the self-test
build\tests.exe      core: rules, adapters, files, language layer
build\gui_tests.exe  interface logic without a window
build\inject_tests.exe  real injection into srtarget.exe
build\routeperapp_probe.exe --selbsttest   window, tray, shortcuts, language
```

`build.cmd` finds Visual Studio Build Tools 2019 or 2022 by itself and needs no project file.
Everything is built with `/MT`, so the result does not need a redistributable.

The last command is the one that catches most mistakes: it starts the real window, hides it,
clicks the tray icon, checks the menus, switches the interface to English and back, and returns the
number of failed points as exit code. It runs without administrator rights through the probe
binary; a non-elevated helper cannot drive the elevated program, because Windows blocks the
messages.

## Conventions

- C++17, MSVC, no third-party libraries and nothing fetched at build time. The JSON layer in
  `src\json.hpp` is deliberately small and local.
- Comments and identifiers in the source are German; keep it that way, the project grew that way
  and the wording in the interface is German as well.
- No exceptions across the injection boundary. The helper DLL must stay small and must never
  assume anything about the host process beyond Winsock.
- `#include <windows.h>` pulls in defines that clash with plain names - `small` is defined as
  `char` by an RPC header, so avoid short dictionary words for variables.
- Text that a user can see goes through `Tr(...)` (see below). Text that is compared with adapter
  names or process names does not.

## Translations

The German source text is the key, the translation is the value in a JSON file. To fix or add a
translation, edit `lang\en.json` and build again, or drop a `language.en.json` into
`C:\ProgramData\RoutePerApp` to test without building.

If you add a visible string in the code, wrap it in `Tr(L"...")`. Run the self-test: it fails while
a visible label has no English translation and writes `language.vorlage.json` with every text the
program uses. `tools\build_en.py` turns that template back into `lang\en.json`.

A new language needs four small changes: an entry in the table at the top of `src\lang.cpp`, a
`lang\<code>.json` file, an `RCDATA` resource for that file in `app.rc` with the id from
`src\resource.hpp`, and one menu item with a new command id in `MakeMenuBar` (`src\gui.cpp`).

## Reports

For a bug: Windows version, RoutePerApp version from the title bar, what you assigned, and what
you measured. Traffic measurements are more useful than impressions - the interface shows bytes per
adapter and per program, and `resolved.json` shows what the engine decided.
