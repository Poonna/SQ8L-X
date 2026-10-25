# SQ8L-X — the SQ8L 64-bit port with parameters for the DAW

> **SQ8L-X** is a fork of [sq8l-revival/SQ8L](https://github.com/sq8l-revival/SQ8L) that
> publishes the sound parameters to the DAW for automation (see *Parameters for the DAW*
> below). It is a plug-in of its own, with its own name, IDs and data folder, so it installs
> next to the original SQ8L and the SQ8L port. Everything below about the port applies to it.

A faithful 64-bit port of **SQ8L 0.91b**, the free Ensoniq SQ-80 emulation by
**Siegfried Kullmann** (2006–2008), for modern macOS (Apple Silicon and Intel), Windows x64
and Linux (x86-64 and ARM64), as **VST2, VST3, Audio Unit (macOS), CLAP and LV2 (Linux)**.

SQ8L was a 32-bit Windows-only VST written in Delphi. Its source code was never released,
and the plugin could no longer run in today's 64-bit hosts. This project rebuilds it from the
original binary so that it can keep being played.

> **Unofficial project.** Not affiliated with Siegfried Kullmann, with Ensoniq or with its
> successors. See [Rights and credits](#rights-and-credits).

![The original editor (left, rendered from the original code) and the port (right)](docs/images/gui_original_vs_port.png)

## How faithful is it?

The port was not written "by ear": every part was translated from the original machine
code and verified against the original plugin running inside an x86 emulator.

- **Sound: bit-exact.** The complete engine produces exactly the same samples as the
  original: 1176 test renders (all 168 factory programs × 7 MIDI scenarios: chords,
  legato, controllers, aftertouch, sustain, voice stealing, random sequences), 120.8 million
  samples per channel, zero differences. Each DSP module (DOC 5503 oscillators, filter,
  LFOs, envelopes, amplifier, voice management) was also verified call by call on real
  captured states — tens of millions of calls in total.
- **Editor: the original's, then improved.** The window was ported from the original's own
  artwork and drawing logic, and is free to improve on it: sharp at any size, faded unused
  knobs, a tidier OPTIONS menu.
- **Compatibility.** Same 512 programs and the same chunk format as the original. Library
  files (`.8XL`), bank files and SQ-80/ESQ-1 SysEx are read and written exactly like the
  original. SQ8L-X has its own plug-in IDs (VST2 `SQ8X`, AU subtype `SQ8X`, its own VST3,
  CLAP and LV2 IDs), so a host sees it as a different plug-in: a project saved with the
  original SQ8L or the SQ8L port keeps using that plug-in and does not open in SQ8L-X. To
  bring a sound across, save it to the library or a bank there and load it in SQ8L-X.

### Differences from the original

- Popup menus, message boxes, file pickers and the WRITE / MIDI-port dialogs use the
  operating system's native controls: on macOS they look like macOS. On Linux they are drawn
  inside the plug-in window (the file picker is the desktop's, or a simple built-in one).
- **The mouse on a knob:** turning one hides the cursor until the button comes up and holds
  it where the turn began, so the pointer never runs into the edge of the screen and stops
  the turn — you can keep dragging as long as you like — and it reappears exactly where it
  was. The original's OPTIONS → *Mouse position is restored after... → Knob turning* became
  *Mouse → Hide cursor when editing*, on by default and shared with the original's
  `restMouseKnob` setting.
- A few small additions to the editor: a right click on a knob opens the list of its values
  (the original has that on a double click, which still works), a knob the display page has
  no parameter for is drawn half faded instead of looking like every other knob, a left click
  on the program number opens the program list (the original needs a right or double click),
  the **VOICES** parameter on the EMU page (see below), and these OPTIONS items: *Output gain*
  (see below), *Polyphony* (see below), *Down arrow -> next program* (the original's hidden
  `swapProgUpDn` setting), *Ask before loading banks/libraries* (on by default, like the
  original), *Support MIDI Program Change* (see below), *MTS-ESP* (see below) and *Zoom*
  (see below). OPTIONS is also regrouped: *Output
  gain* leads, then *Polyphony*, and the original's mouse-restore submenu and its separate
  *Right click on display -> scroll page* item are now one *Mouse* submenu.
- **Window size:** the editor can be enlarged from 100% to 300% with OPTIONS → *Zoom* (or
  the window's corner, where the host allows it). It is drawn at the window's resolution
  rather than enlarged: the panel (labels, signal lines, frames, logo) as vector shapes
  measured from the original's picture, the knobs, buttons, LEDs and both displays from
  their state. The size is global (saved in `SQ8L.ini`).
- **MTS-ESP:** OPTIONS → *MTS-ESP* → *Enable* makes the plug-in follow an MTS-ESP master's
  tuning, for microtuning and dynamic scales (off by default; the master's scale name then
  appears in the top bar). Each voice is retuned by its own resampling clock rather than by the
  SQ-80's pitch table, which is quantized to about 3 cents: the tuning is exact to a millionth
  of a cent while the oscillators, their relative detune and the sound itself are untouched.
  By default only the master's deviation from equal temperament is applied, so the SQ-80's own
  per-key pitch offsets (−1.5 to +0.5 cents, part of how it sounds) are preserved; *Correct
  SQ-80 per-key pitch offsets* tunes to the master's frequencies exactly instead. Pitch bend
  and glide keep the original's stepping either way. Both switches belong to the instance:
  they are saved with it in your project, not globally, so two instances can differ and a
  project recalls what it was set to.
- **Program changes:** the original ignores them; OPTIONS → *Support MIDI Program Change*
  makes a program change select a sound. A program change picks that program number, and CC0
  (bank select) picks the bank A-D for the program changes that follow — until a CC0 arrives,
  a program change loads from whichever bank the editor is showing, and a CC0 naming a bank
  that does not exist makes the program change be ignored rather than snap to bank D. Both are
  received on any MIDI channel, like notes. The switch belongs to the instance and is saved
  with it in your project: a new instance responds, while a project saved by the original or
  by an earlier version of this port keeps ignoring program changes, so nothing you already
  have changes sound. Note that a program change replaces the edit buffer, so unsaved edits
  to the current sound are lost — the same as picking another program in the editor, which can
  at least ask first. The SQ-80 had no bank select at all: its 120 sounds were one flat range
  of program numbers (1-40 internal, 41-80 and 81-120 the two cartridge banks), which does not
  reach 512.
- **Output gain:** most soundbanks were written far louder than a modern project wants, so
  OPTIONS → *Output gain* cuts the instance's output by 6, 12, 18 or 24 dB (no cut by
  default). Each step is a halving, which only shifts the exponent of every sample: the sound
  is the same, quieter, with nothing added and no precision lost. The cut belongs to the
  instance and is saved with it in your project, so two instances can differ and a project
  recalls what it was set to; the value you last picked is also remembered globally (in
  `SQ8L.ini`) as the cut a newly created instance starts at. A project always wins over that:
  loading one that was saved with no cut puts the instance at 0 dB.
- **Polyphony:** 8 voices like the SQ-80 by default, 1 to 64 with the **VOICES** parameter on
  the EMU page, right below VSTEAL. It belongs to the sound, so it is stored in the program
  and travels with it: into banks and libraries, and into the project your host saves. Every
  program that does not have it — anything made by the original SQ8L, any `.8XL` or bank
  file, any imported SysEx dump — plays with the SQ-80's 8 voices, exactly as it always did,
  and the original can still load a bank the port saved. OPTIONS → *Polyphony* overrides the
  parameter for one instance (*Set by program* by default) and is saved with that instance in
  your project, not globally. Changing the voices never interrupts anything: the notes that
  are playing keep playing, and soft voice stealing gets as many slots to fade out in as there
  are voices. With 8 voices the sound is bit-exact; with more, a performance changes only
  where the original would have stolen a voice. The output is not rescaled, so many voices
  sounding together are louder: use *Output gain* above if needed.
- **Parameters for the DAW (SQ8L-X):** the original publishes none, so a host could neither
  automate the sound nor show it. The plug-in now publishes the 228 sound parameters of the
  display pages, named after the page and the field as the display shows them (`OSC1 WAVE`,
  `FILT FREQ`, `LFO2 FMOD.SEL`, `EMU VOICES`...), with their ranges and, where the display
  names the values, the same names (`SAW`, `ENV3`, `TRI`...). A parameter is a field of the
  current program: automating it edits the program like turning its knob (the display follows),
  and selecting another program, loading a chunk or turning a knob in the editor changes what
  the host sees. Knob turns are reported to the host as one gesture each, so a touch or latch
  automation mode records them. Chunks are unchanged: the program, not the parameter list,
  is what is saved.
  The editor is what tells the host about changes, so with the editor closed a MIDI program
  change is heard but not shown in the host until the editor is opened again.
- Text (status bar, program name) uses Liberation Sans, a free font metrically compatible
  with Arial, with anti-aliasing.
- SEND/REQ to a hardware SQ-80/ESQ-1 over MIDI ports are not connected yet (SysEx import and
  export through files work).
- Two original bugs could not be reproduced and were made safe: an integer division by zero
  in the envelope SHAPE code for a few rare level/velocity combinations (the original
  crashed), and an uninitialised byte read in the "AM bug" emulation when oscillator 2 is
  edited while a note plays (the port uses the intended value).
- As in the original, sample rates below 44.1 kHz are not supported.

## Installation

Download the archive for your system from the
[Releases](../../releases) page. The numbered releases have been tried by hand; the
*nightly* pre-release is rebuilt from `main` after every merged pull request, once it has
passed the same automatic checks.

**macOS** (10.15 or later, universal): copy `SQ8L-X.component` to
`~/Library/Audio/Plug-Ins/Components`, `SQ8L-X.vst3` to `~/Library/Audio/Plug-Ins/VST3`,
`SQ8L-X.vst` to `~/Library/Audio/Plug-Ins/VST`, `SQ8L-X.clap` to `~/Library/Audio/Plug-Ins/CLAP`.
The plug-ins are not notarized by Apple, so macOS blocks them when they come from the
internet: remove the quarantine flag in Terminal, then restart your DAW.

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/SQ8L-X.component ~/Library/Audio/Plug-Ins/VST3/SQ8L-X.vst3 ~/Library/Audio/Plug-Ins/VST/SQ8L-X.vst ~/Library/Audio/Plug-Ins/CLAP/SQ8L-X.clap
```

**Windows** (10/11, 64-bit): copy `SQ8L-X.dll` (VST2) to your VST plug-in folder,
`SQ8L-X.vst3` to `C:\Program Files\Common Files\VST3`, `SQ8L-X.clap` to
`C:\Program Files\Common Files\CLAP`. The files do not clash with the original's or the
port's `SQ8L.*`: all of them can be installed together.

**Linux** (x86-64 or ARM64, X11 or XWayland): unpack `SQ8L-X-Linux-x64.tar.gz` (or `-arm64`)
and copy `SQ8L-X.so` (VST2) to `~/.vst`, `SQ8L-X.vst3` to `~/.vst3`, `SQ8L-X.clap` to
`~/.clap` and `SQ8L-X.lv2` to `~/.lv2`. Banks A and B and the options are kept in
`~/.config/SQ8L-X`.

The user library (banks A and B) and the options are stored in
`~/Library/Application Support/SQ8L-X` (macOS), `%APPDATA%\SQ8L-X` (Windows) or
`~/.config/SQ8L-X` (Linux) as `SQ8L_backup.dat` and `SQ8L.ini`, the same files the original
kept in its folder. The folder is SQ8L-X's own: the library is written back whole when the
last instance closes, so sharing the SQ8L port's folder would let each overwrite the other's
edits. The first time, SQ8L-X starts from the SQ8L port's library and options (its `SQ8L`
folder, which it never writes to), so your banks come along. To use another library, copy
its `SQ8L_backup.dat` into the SQ8L-X folder (or load it with *FILE → Load library*).

The manual of the original (`readme.txt` in the SQ8L 0.91b archive) applies unchanged.

## Building from source

Requirements: CMake ≥ 3.22, Ninja, a C++17 compiler (Clang, GCC or MSVC), Git.

```sh
git clone --recursive <this repository>
cmake -S . -B build -G Ninja
cmake --build build
```

- macOS universal: add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15`.
- Linux: install the X11 and OpenGL development packages first (Debian/Ubuntu:
  `libx11-dev libxext-dev libxcursor-dev libxrandr-dev libgl-dev libdbus-1-dev`).
- Windows x64 from macOS/Linux with MinGW-w64:
  `-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake` (Linux: use the `-posix` compilers).
- Plug-ins are written to `build/bin/`.

On Windows, build natively with Visual Studio 2022 instead (`cl.exe` or `clang-cl`, both
x64) through the presets in `CMakePresets.json`, which write to `build/<preset>/bin/`:

```sh
cmake --preset msvc        # or: clang-cl, msvc-debug, clang-cl-debug
cmake --build --preset msvc
```

In VS Code, pick the preset in the status bar and press F7; the CMake Tools extension
supplies the x64 developer environment. Run the presets from a Developer Command Prompt
otherwise. Building `third_party/core-math` with `cl.exe` needs `src/compat/core_math_msvc.hpp`,
which supplies the 128-bit integer type and the GCC builtins MSVC lacks.

Check that the engine still reproduces the original (no original files needed):

```sh
cmake --build build --target sq8l_render_check
./build/sq8l_render_check --regression tests/regression
```

This renders 243 MIDI cases and compares a hash of the output with the hash of the original
plugin's output.

## How it was made

- `oracle/` — a small Win32 emulator (Unicorn) that runs the **original** `SQ8L.dll` on any
  platform: it renders audio through the VST2 interface and even opens and drives the
  original editor (USER32/GDI emulated on a framebuffer). It is the reference for all tests.
- `re/scripts/` — analysis tools: Delphi class/RTTI recovery, form (DFM) decoding, Ghidra
  export per Delphi unit, data extraction.
- `tests/` — differential tests: real calls of the original routines are captured in the
  emulator and replayed on the C++ code, which must give identical results and state.
- `docs/` — [porting guide](docs/PORTING_GUIDE.md) (x87 floating point semantics, Delphi
  conventions, method), [GUI architecture](docs/GUI_ARCHITECTURE.md) and one document per
  module of the original (`docs/modules/`).
- `src/engine/` the synth engine, `src/gui/` the editor, `src/plugin/` the plug-in
  (DPF) and platform layers (macOS, Windows).

The emulator and the differential tests need the original plugin, which is **not**
included. Put `SQ8L.dll` (v0.91b, SHA-256
`ffbb70884a6cd2af9600d287deebb2ba8dc6ec2270150ceef04c23d0fc92ab0e`) and `SQ8L.ini` from
the original archive in `original/`, install `requirements.txt`, and run
`tools/prepare_reference.sh`.

## Rights and credits

**SQ8L** © 2006–2008 Siegfried Kullmann. Factory sounds in bank C by Ole Jeppesen and
Siegfried Kullmann; bank D contains the Ensoniq SQ-80 factory sounds. Thanks to Rainer
Buchty "for SQ-80 resources & disassembled OS code", as in the original.

This port reproduces SQ8L's behaviour, artwork and data (including the SQ-80 wave ROM and
factory sounds that the original plugin contained) to preserve a freeware instrument that
could no longer run. It is free, non-commercial and published in good faith. The rights
to the original work belong to Siegfried Kullmann; Ensoniq, ESQ-1 and SQ-80 are
trademarks of their respective owners.

If you hold rights to any part of this work and object to its publication, please
[open an issue](../../issues) (you can ask there for a private contact) and it will be
addressed promptly.

Third-party components and their licences: see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Licence of this repository: see [LICENSE.md](LICENSE.md).
