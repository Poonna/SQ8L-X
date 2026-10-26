<!-- Notes for this version: add what changed here before publishing the draft. -->

**SQ8L-X: the unofficial 64-bit port of SQ8L 0.91b** (Siegfried Kullmann's Ensoniq SQ-80
emulation) **with its sound parameters published to the DAW** for automation, for macOS
(universal: Apple Silicon and Intel, 10.15 or later), Windows x64 (10/11) and Linux (x86-64
and ARM64), as VST2, VST3, Audio Unit (macOS), CLAP and LV2 (Linux).

The sound is bit-identical to the original plug-in: this build was checked against the
original's output on Apple Silicon, on Intel, on Windows and on Linux. Libraries, banks and
SysEx files made with the original load unchanged. SQ8L-X is a plug-in of its own (its own
name and IDs), so it installs next to SQ8L, and projects saved with SQ8L keep using SQ8L.
See the [README](https://github.com/Poonna/SQ8L-X#readme) for details, credits and the known
differences.

### macOS

1. Unzip `SQ8L-X-macOS-universal.zip` and copy the formats you use:
   - `SQ8L-X.component` (Audio Unit) to `~/Library/Audio/Plug-Ins/Components`
   - `SQ8L-X.vst3` to `~/Library/Audio/Plug-Ins/VST3`
   - `SQ8L-X.vst` (VST2) to `~/Library/Audio/Plug-Ins/VST`
   - `SQ8L-X.clap` to `~/Library/Audio/Plug-Ins/CLAP`
2. The plug-ins are not notarized by Apple, so macOS blocks them when they come from the
   internet. Open Terminal and run (an error for a format you did not install is harmless):
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/SQ8L-X.component ~/Library/Audio/Plug-Ins/VST3/SQ8L-X.vst3 ~/Library/Audio/Plug-Ins/VST/SQ8L-X.vst ~/Library/Audio/Plug-Ins/CLAP/SQ8L-X.clap
   ```
3. Restart your DAW and rescan the plug-ins.

### Windows

Unzip `SQ8L-X-Windows-x64.zip` and copy `SQ8L-X.dll` (VST2) to your VST plug-in folder,
`SQ8L-X.vst3` to `C:\Program Files\Common Files\VST3`, `SQ8L-X.clap` to
`C:\Program Files\Common Files\CLAP`. They do not clash with SQ8L's files.

### Linux

Unpack `SQ8L-X-Linux-x64.tar.gz` (or `SQ8L-X-Linux-arm64.tar.gz` on ARM) and copy `SQ8L-X.so`
(VST2) to `~/.vst`, `SQ8L-X.vst3` to `~/.vst3`, `SQ8L-X.clap` to `~/.clap` and `SQ8L-X.lv2`
to `~/.lv2`. The editor needs X11 (or XWayland). The Linux version is new: reports are welcome.

### Your sounds

Banks A and B and the options are stored in `~/Library/Application Support/SQ8L-X` (macOS),
`%APPDATA%\SQ8L-X` (Windows) or `~/.config/SQ8L-X` (Linux). The first time, SQ8L-X starts
from the SQ8L port's banks and options, if it finds them. To use another library, copy its
`SQ8L_backup.dat` there, or load it with *FILE → Load library*.

Problems or questions: please [open an issue](https://github.com/Poonna/SQ8L-X/issues).
