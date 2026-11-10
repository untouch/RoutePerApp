# Changes

The version number lives in `src\resource.hpp`; the file properties, the title bar and the About
dialog all read it from there.

## 1.1 - 2026-11-10

### Interface in two languages

The interface can be switched between German and English from ? -> Sprache, or with
`--sprache en` at start; the choice is remembered in `settings.json`. The German wording in the
source is the translation key, the values sit in JSON files:

- `lang\en.json` is built into the program.
- `language.en.json` next to the executable or in `C:\ProgramData\RoutePerApp` overrides it, so a
  wording can be corrected without building again.
- The self-test writes `language.vorlage.json` with every text the program uses, which makes
  missing values a file to fill in rather than a hunt through the source.
- `tools\build_en.py` turns that template back into `lang\en.json`.

### Tests

- The self-test now rebuilds the whole interface in English and checks menus, window title, button
  captions and column headings. A visible label without a translation fails the test and is
  reported by name.
- 30 new checks for the language layer: file override, comments in the language file, keys without
  a value, address stability of translated text, unreadable language file.
- Totals: 364 core, 129 interface, 89 integration checks, no failures.
- The injection test no longer reports a failure when the invented adapter in that section has no
  route in the real routing table: the check is about the attempt, and delivery over a made-up
  network was never the point.

### Fixed

- Adapter search words ("hyper-v", "wan miniport", "zero tier", ...) were run through the
  translation layer. In an English interface that would have misclassified real adapters. They are
  plain text again.
- Typo in the status line shown without administrator rights ("Zuordnung müglich").
- Version kept in one place instead of two, so the file properties cannot disagree with the About
  dialog.

### Documentation

`SECURITY.md` (what the program does that anti-virus reacts to, why administrator rights, the trust
boundary of the shared folder) and `CONTRIBUTING.md` (build loop, conventions, translations) are
new; the README has a Language section and answers to the questions people actually ask.

The README now shows the window itself: `assets\screenshot-en.png` and `assets\screenshot-de.png`.
They come from `tools\shot.cpp` (`build.cmd shot`), which paints the program's own window into a
PNG - no capture script involved, and nothing is asked of any other program.

### Next

- Write access for ordinary users in `C:\ProgramData\RoutePerApp` is a local privilege escalation
  path for an elevated program that injects from that folder; closing it is the next change.
- A small route control on the window that is in front, and a command line for the rule file.

## 1.0 - 2026-11-09

First published version.

- Per-program routing: Wi-Fi, Ethernet or the normal Windows route, chosen per executable, stored
  as rules and applied again to programs that start later.
- Winsock binding through injected helper DLLs, 64-bit and 32-bit targets, chosen by target
  bitness. No VPN, no TUN adapter, no proxy, no kernel driver.
- Interface with process list, three route cards, drag and drop, undo, search, per-adapter traffic
  counters, details per program.
- Default route for programs without their own rule; global shortcuts for the window in front;
  autostart as a scheduled task; close hides to the notification area and keeps routing.
- Rules in `C:\ProgramData\RoutePerApp\rules.json`, reloaded on content change, with a backup copy.
- Built-in self-test (`--selbsttest`) for the parts that are otherwise only observable from
  outside, plus 552 automated checks in three test programs.
- GPL-3.0, single build script, no third-party libraries, nothing downloaded while building.
