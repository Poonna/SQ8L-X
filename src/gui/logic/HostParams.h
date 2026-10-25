// (port) The sound parameters the plugin publishes to the host, for automation and the
// host's own controls. The original publishes none: the editor is the only way in.
//
// They are the parameters of the editor's pages (buildLcdPages), so the host sees exactly
// what the display shows -- the same names, ranges and value texts -- each one once even
// where two pages show it (OSC1-3 repeats the oscillator pages). A parameter is a field of
// the edit buffer: setting it is an edit like a knob turn, and its value is the edit
// buffer's, so a program change, a chunk or the editor changes what the host reads.
//
// The order is that of the pages and must not change: hosts store automation by index.
#pragma once

#include <string>
#include <vector>

namespace sq8l {
class EditBuffer;
}

namespace sq8l::gui {

struct HostParam {
    std::string name;    // the display's sub-page tag + field name: "OSC1 OCT", "FILT FREQ"
    std::string symbol;  // the name as an identifier: "osc1_oct", "lfo2_fmod_sel"
    std::string hint;    // the status bar text of the field
    int editIndex = -1;  // EditBuffer parameter
    int min = 0, max = 0;
    int def = 0;         // the INIT program's value
    bool voices = false; // EMU VOICES, where a program's 0 means the original's 8 voices
    // The display's text of every value from min to max, for the parameters the display
    // names rather than numbers (waves, modulation sources, modes...); empty for numbers.
    std::vector<std::string> valueTexts;
};

const std::vector<HostParam>& hostParams();

// The value the display shows. That is the edit buffer's, except for VOICES, where the 0 of
// every program the original made means its 8 voices (LcdController::readParam).
int hostParamValue(const EditBuffer& eb, const HostParam& p);
// Clamps to the range and writes only when the value changes, so a host sending back the
// value it was given leaves the program untouched (and not marked as modified). True when
// the edit buffer changed.
bool setHostParamValue(EditBuffer& eb, const HostParam& p, int value);

}  // namespace sq8l::gui
