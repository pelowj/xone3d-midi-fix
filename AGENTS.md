# Notes for AI coding agents

This repo fixes USB MIDI on the Allen & Heath Xone:3D under current macOS by flashing a patched
copy of the official firmware 1.26. Flashing firmware can make the mixer unusable. You are probably
helping someone who isn't comfortable doing this themselves, so be careful and keep them informed.
Read `README.md` and `PROTOCOL.md` for the background.

## Hard rules

- **Never run `x3dfix --flash` unless the user has explicitly told you to flash, in this
  conversation, after you've told them the risk.** "Try again" or "fix it" after a failed run is not
  permission to flash again; ask.
- Only use `--yes` (it skips the tool's own typed confirmation) once the user has given that
  explicit go-ahead. Never pipe input into the tool to get past the confirmation.
- **Never modify the patch, the offsets, the SHA-256 values or the packet size in `x3dfix.c`.**
  In particular don't "correct" 8 to 64: the firmware's hardware buffer for that endpoint is
  8 bytes, and 64 would overflow it.
- Never flash any file other than the official `XONE3D 1.26.BIN`. If the tool rejects the file,
  stop and tell the user. Don't patch or edit the file to make it pass.
- Never send other vendor requests to the mixer (including request 2, which changes its settings)
  unless the user asks for that specifically.
- Don't install or run other firmware tools, drivers or downloads.

## Normal workflow

1. Build: `make` (needs the Xcode command line tools).
2. Check the mixer: `./x3dfix --check` (reads only).
   - Exit 0, `PATCHED`: the fix is already installed. Don't flash again. If MIDI isn't online, have
     the user power-cycle the mixer and replug USB.
   - Exit 10, `STOCK`: the fix is not installed. Continue.
   - Anything else (not found, `UNKNOWN`, status `BUSY`): stop and explain to the user.
3. Dry run: `./x3dfix "XONE3D 1.26.BIN"` (reads only). It must report the official 1.26 image.
4. Explain the risk and ask the user for an explicit go-ahead to flash.
5. Flash. The update takes about a minute but can take longer, so don't run it as a normal
   foreground command with a short timeout. Run it in the background, logging to a file, and read
   the log as it goes:

   ```sh
   caffeinate -dims ./x3dfix --flash --yes "XONE3D 1.26.BIN" > flash.log 2>&1
   ```

   **Never kill or interrupt the process during an update.** It ignores Ctrl-C on purpose and stops
   by itself (within about 60 s) if the mixer stalls.
6. When the log says `firmware update complete`, ask the user to power the mixer off and on again,
   then run `./x3dfix --check` again. Expect `PATCHED` and `Xone3d online`.
7. Ask the user to move a few controls and confirm their DJ software (or Audio MIDI Setup) sees MIDI,
   and that the mixer's own functions (mixer audio, BPM display, LFO, button lights) work.

## If something goes wrong

- **The update stalls** (`MIXER STAYED BUSY`, exit 4) or **fails** (exit 3), or after the power
  cycle the mixer's display, mixer section, LFO or buttons are dead: ask the user to power-cycle
  the mixer, run `--check`, then, with their go-ahead, flash again straight away. A repeated update
  has fixed this every time so far.
- If the mixer doesn't appear on USB at all after a power cycle, stop. Don't try anything else;
  tell the user it may need an EEPROM programmer or Allen & Heath's help.
- If the user's USB connection goes through an adapter, suggest a powered hub or a direct port
  before retrying.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success (`--check`: fix installed) |
| 1 | Error before anything was written (file rejected, mixer not found, cancelled) |
| 2 | Bad arguments |
| 3 | Write or status error during the update |
| 4 | Mixer stayed busy during the update |
| 10 | `--check`: stock firmware, fix not installed |
