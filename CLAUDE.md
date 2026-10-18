# WeatherFields

At the start of every session, read `PROJECT_GUIDE.md`: purpose, status, next
actions, contracts, conventions, build commands and decisions. Then
`docs/HANDOVER-ModularSystem.md` for how the user works, the platform lessons
and the milestones. Where the two disagree, the guide wins.

- Work in small steps. The user tests each one (usually by listening), often
  asks for a review, then commits. Never commit or push unless asked.
- After each step, edit `PROJECT_GUIDE.md` in place (status, next actions,
  decisions); never append a log.
- When the user pastes a review finding, check it against the code before
  changing anything.
- Before calling a step done: `cmake --build --preset dev && ctest --preset dev`
  (it includes the Clouds render references), and `Tools/Bela/check.sh` for
  code below Platforms.
- `Modules/MutableDSP` is a submodule with its own import and patch policy
  (`UPSTREAM.md`, `PATCHES.md`); commit there on `main`, not detached HEAD.
- `References/` holds local reference material and is not tracked.
