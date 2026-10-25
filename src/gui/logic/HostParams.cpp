#include "HostParams.h"

#include <cctype>
#include <set>

#include "EditBuffer.h"
#include "Formatters.h"
#include "LcdControl.h"
#include "VoiceSlots.h"

namespace sq8l::gui {

namespace {

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(' ');
    if (b == std::string::npos) return std::string();
    return s.substr(b, s.find_last_not_of(' ') - b + 1);
}

std::string symbolOf(const std::string& name) {
    std::string s;
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)))
            s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (!s.empty() && s.back() != '_')
            s += '_';
    }
    while (!s.empty() && s.back() == '_') s.pop_back();
    return s;
}

// The tag the display shows at the start of the sub-page ("[FILT]", "[EMU]", "[MAT1]"): the
// label of its first field when that field has no value. Short enough that every name fits
// the 15 characters VST2 hosts show; the page menu's title where there is no tag.
std::string prefixOf(const LcdSubPage& sp) {
    if (!sp.params.empty() && sp.params[0].name.empty()) {
        const std::string tag = trim(sp.params[0].label);
        if (!tag.empty()) return tag;
    }
    return sp.title;
}

// The formatters that name values; the three defaults (OFF/ON aside) only pad numbers.
bool namesValues(Fmt f) { return f != Fmt::None && f != Fmt::Unsigned && f != Fmt::Signed && f != Fmt::Polyphony; }

std::vector<HostParam> build() {
    const std::vector<LcdPage> pages = buildLcdPages();
    const Program init = makeInitProgram();
    std::vector<HostParam> out;
    std::set<int> seen;
    // The pages in order, the OSC1-3 overview (page 0) last: everything on it is also on the
    // page of its oscillator, where it has the oscillator's name.
    for (int n = 1; n <= int(pages.size()); n++) {
        const LcdPage& page = pages[size_t(n % int(pages.size()))];
        for (const LcdSubPage& sp : page.subs)
            for (const LcdParam& lp : sp.params) {
                const int idx = sp.editBufferIndex(lp);
                if (lp.name.empty() || idx < 0 || idx >= kNumParams || !seen.insert(idx).second) continue;
                HostParam p;
                p.name = prefixOf(sp) + " " + lp.name;
                p.symbol = symbolOf(p.name);
                p.hint = lp.hint;
                p.editIndex = idx;
                p.min = lp.min;
                p.max = lp.max;
                p.def = getParam(init, idx);
                p.voices = lp.fmt == Fmt::Polyphony;
                if (p.voices && p.def == 0) p.def = kOriginalPlayableVoices;
                if (namesValues(lp.fmt))
                    for (int v = p.min; v <= p.max; v++) {
                        std::string s;
                        formatValue(lp.fmt, lp.width, v, s);
                        s = trim(s);
                        p.valueTexts.push_back(s.empty() ? delphi::intToStr(v) : s);
                    }
                out.push_back(std::move(p));
            }
    }
    return out;
}

}  // namespace

const std::vector<HostParam>& hostParams() {
    static const std::vector<HostParam> params = build();
    return params;
}

int hostParamValue(const EditBuffer& eb, const HostParam& p) {
    const int v = eb.param(p.editIndex);
    if (v == 0 && p.voices) return kOriginalPlayableVoices;
    return v;
}

bool setHostParamValue(EditBuffer& eb, const HostParam& p, int value) {
    if (value < p.min) value = p.min;
    if (value > p.max) value = p.max;
    if (value == hostParamValue(eb, p)) return false;
    eb.setParamValue(p.editIndex, value);
    return true;
}

}  // namespace sq8l::gui
