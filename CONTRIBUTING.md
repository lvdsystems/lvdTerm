# Contributing to lvdterm

Thanks for considering it. This is a young project, so process is
deliberately light — most of what follows exists to keep the codebase
easy for the next person (including future-you) to pick up, not to gate
contributions.

## Before you start

For anything beyond a small, obvious fix, please open an issue (or
comment on an existing one) before writing code — it's a much cheaper way
to align on approach than reworking a finished PR. See the [issue
templates](.github/ISSUE_TEMPLATE) for bug reports and feature requests,
and the README's comparison table for what's deliberately out of scope
for now. [`TODO.md`](TODO.md) lists concrete, unclaimed work if you want
a starting point rather than bringing your own idea.

## Building

See the [README](README.md#building-from-source) for prerequisites and
build commands. In short: Qt 6 with an llvm-mingw kit, CMake 3.25+, and
you'll need to point `CMakePresets.json` (or a local
`CMakeUserPresets.json`) at wherever Qt and the toolchain actually live
on your machine.

Build **both** presets (`llvm-mingw` Debug, `llvm-mingw-release` Release)
before opening a PR — a change that only compiles in one has happened
before.

## Code style

- Match the surrounding code, not a style guide. This project doesn't
  run a formatter; consistency with the file you're editing matters more
  than any abstract preference.
- **Comments explain *why*, not *what*.** A well-named function or
  variable already says what it does; a comment is for the part a reader
  can't get from the code alone — a non-obvious constraint, the reason
  behind a workaround, or a decision that looks wrong until you know the
  context. If you'd delete a comment during review because it just
  restates the line below it, don't add it in the first place.
- Prefer the smallest change that correctly fixes the problem over a
  broader refactor, even if the refactor is tempting. Small, focused PRs
  are much easier to review and to revert if something's wrong.
- Don't add error handling, config flags, or abstraction for cases that
  can't currently happen. It's easier to add a seam later, once a second
  real use case shows up, than to guess at one now.

## The vendored terminal core

`third_party/qtermwidget/` is a stripped-down, Windows-ported copy of
Konsole/QTermWidget, not code this project owns outright. Changes to
anything under `third_party/` should be:

- **As small as possible** — a one-method patch, not a rewrite.
- **Documented in [`VENDORING.md`](third_party/qtermwidget/VENDORING.md)**
  under "Patches applied to vendored files," with enough context that
  someone re-vendoring a newer upstream QTermWidget later knows exactly
  what to reapply and why.

If what you need is achievable in lvdterm's own code (`src/`) using the
vendored core's existing public API, prefer that over touching vendored
files at all.

The same logic applies to `third_party/libssh2/` and
`third_party/openssl-prebuilt/` — see their own `VENDORING.md`/
`BUILDING.md`.

## Testing your change

There's no CI or formal test suite wired into the CMake build yet.
What's expected instead:

- Build both presets clean.
- Actually launch the app and exercise the path you changed — this
  project has been burned before by changes that compiled fine but
  crashed or misbehaved the moment they ran.
- For logic that's awkward to click through by hand (parsing, model
  behavior, an emulation-core code path), a small standalone program that
  links against the real classes and asserts on the real behavior is far
  more convincing than reasoning about it from reading the code, and
  costs less than it sounds like — several such throwaway checks exist in
  this project's history for exactly that reason.
- If you're touching mouse/keyboard/drag-and-drop handling, please don't
  reach for OS-level input automation to test it (this project has been
  burned by that too, in the direction of an editor firing real
  keystrokes into whatever window happened to have focus). Synthetic
  in-process Qt events (`QApplication::sendEvent()` with a constructed
  `QMouseEvent`/`QKeyEvent`) are the safe equivalent for automated
  checks; a manual, actually-clicked pass is the alternative.

## Submitting a PR

- Describe the *why*, not just the *what* — the diff already shows what
  changed.
- Mention how you verified it (built + launched, a specific repro,
  a standalone check, etc.).
- Keep it focused. A bug fix doesn't need to also refactor its
  neighborhood.

## License

lvdterm is licensed under GPL-3.0-or-later (see [`LICENSE`](LICENSE)).
By submitting a contribution, you agree it's provided under the same
license. See [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md) before
adding any new dependency — license compatibility with GPL-3.0-or-later
isn't automatic, and vendoring something GPL-incompatible is a much
bigger problem to unwind later than to check for up front.
