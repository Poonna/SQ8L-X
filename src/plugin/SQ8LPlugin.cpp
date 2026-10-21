#include "SQ8LPlugin.hpp"

#include <cmath>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

String toHex(const std::vector<uint8_t>& bytes) {
    static const char digits[] = "0123456789abcdef";
    std::vector<char> s(bytes.size() * 2 + 1);
    for (size_t i = 0; i < bytes.size(); i++) {
        s[2 * i] = digits[bytes[i] >> 4];
        s[2 * i + 1] = digits[bytes[i] & 15];
    }
    s[bytes.size() * 2] = '\0';
    return String(s.data());
}

std::vector<uint8_t> fromHex(const char* s) {
    std::vector<uint8_t> out;
    const auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        c = static_cast<char>(c | 0x20);
        return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    };
    for (size_t i = 0; s[i] && s[i + 1]; i += 2) {
        const int hi = nib(s[i]), lo = nib(s[i + 1]);
        if (hi < 0 || lo < 0) break;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

void copyName(char* out, const std::string& s) {
    std::strncpy(out, s.c_str(), 24);
    out[23] = '\0';
}

}  // namespace

SQ8LPlugin::SQ8LPlugin()
    : Plugin(static_cast<uint32_t>(sq8l::gui::hostParams().size()), sq8l::Synth::kNumPrograms, 1),
      shared_(sq8l::SharedLibrary::acquire()),
      synth_(static_cast<float>(getSampleRate() > 0 ? getSampleRate() : 44100.0), shared_.library, shared_.settings,
             this),
      events_(1024) {
    synth_.setSampleRate(static_cast<float>(getSampleRate()));
    // (port) MTS-ESP: the engine reads this snapshot, run() refreshes it per block.
    synth_.setTuning(&mts_.tuning());
    // The host starts out believing every parameter is at its default (initParameter).
    for (const sq8l::gui::HostParam& p : sq8l::gui::hostParams()) hostValues_.push_back(p.def);
    echoUntil_.resize(hostValues_.size());
}

SQ8LPlugin::~SQ8LPlugin() { sq8l::SharedLibrary::release(); }

void SQ8LPlugin::vst2SetProgram(int32_t index) {
    if (index < 0 || index >= sq8l::Synth::kNumPrograms) return;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    synth_.setProgram(index);
    hostKnowsParameters();
}

int32_t SQ8LPlugin::vst2GetProgram() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return synth_.program();
}

void SQ8LPlugin::vst2SetProgramName(const char* name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    synth_.setProgramName(name);
}

void SQ8LPlugin::vst2GetProgramName(char* out) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    copyName(out, synth_.programName());
}

bool SQ8LPlugin::vst2GetProgramNameIndexed(int32_t index, char* out) {
    if (index < 0 || index >= sq8l::Synth::kNumPrograms) return false;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    copyName(out, synth_.programNameIndexed(index));
    return true;
}

void SQ8LPlugin::initProgramName(uint32_t index, String& name) {
    name = synth_.programNameIndexed(static_cast<int32_t>(index)).c_str();
}

void SQ8LPlugin::loadProgram(uint32_t index) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    synth_.setProgram(static_cast<int32_t>(index));
    hostKnowsParameters();
}

// ---- (port) parameters

void SQ8LPlugin::initParameter(uint32_t index, Parameter& parameter) {
    const auto& params = sq8l::gui::hostParams();
    if (index >= params.size()) return;
    const sq8l::gui::HostParam& p = params[index];
    parameter.hints = kParameterIsAutomatable | kParameterIsInteger;
    if (p.min == 0 && p.max == 1) parameter.hints |= kParameterIsBoolean;
    parameter.name = p.name.c_str();
    parameter.symbol = p.symbol.c_str();
    parameter.description = p.hint.c_str();
    parameter.ranges.min = static_cast<float>(p.min);
    parameter.ranges.max = static_cast<float>(p.max);
    parameter.ranges.def = static_cast<float>(p.def);
    if (!p.valueTexts.empty()) {  // the display's names for the values (at most 147 of them)
        const uint32_t n = static_cast<uint32_t>(p.valueTexts.size());
        ParameterEnumerationValue* values = new ParameterEnumerationValue[n];
        for (uint32_t i = 0; i < n; i++) {
            values[i].label = p.valueTexts[i].c_str();
            values[i].value = static_cast<float>(p.min + static_cast<int>(i));
        }
        parameter.enumValues.count = static_cast<uint8_t>(n);
        parameter.enumValues.restrictedMode = true;
        parameter.enumValues.values = values;
    }
}

float SQ8LPlugin::getParameterValue(uint32_t index) const {
    const auto& params = sq8l::gui::hostParams();
    if (index >= params.size()) return 0.0f;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return static_cast<float>(sq8l::gui::hostParamValue(const_cast<sq8l::Synth&>(synth_).editBuffer(), params[index]));
}

// An edit like a knob turn, written only when the value changes (setHostParamValue). Not
// while the editor is reporting this parameter: the host is then handing back what it was
// told, and an older value of a knob that has moved on since must not turn it back.
void SQ8LPlugin::setParameterValue(uint32_t index, float value) {
    const auto& params = sq8l::gui::hostParams();
    if (index >= params.size()) return;
    const int v = static_cast<int>(std::lround(value));
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    hostValues_[index] = v;
    if (std::chrono::steady_clock::now() < echoUntil_[index]) return;
    sq8l::gui::setHostParamValue(synth_.editBuffer(), params[index], v);
}

// The host chose the program, and reads the parameters of it: nothing to report. (A chunk is
// different: one saved before the port published parameters carries none, so the host still
// has its old values, and the editor reports the chunk's.)
void SQ8LPlugin::hostKnowsParameters() {
    const auto& params = sq8l::gui::hostParams();
    for (size_t i = 0; i < params.size(); i++)
        hostValues_[i] = sq8l::gui::hostParamValue(synth_.editBuffer(), params[i]);
}

void SQ8LPlugin::initState(uint32_t index, State& state) {
    if (index == 0) {
        state.key = "editbuffer";
        state.defaultValue = "";
        state.label = "Edit buffer";
        state.hints = kStateIsOnlyForDSP;
    }
}

// The chunk also carries OPTIONS -> Polyphony (Synth::getChunk), so one state key is all the
// plugin needs: the VST2 chunk is this value verbatim (DISTRHO_PLUGIN_VST2_RAW_CHUNK_KEY).
String SQ8LPlugin::getState(const char* key) const {
    if (std::strcmp(key, "editbuffer") != 0) return String();
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return toHex(const_cast<sq8l::Synth&>(synth_).getChunk());
}

void SQ8LPlugin::setState(const char* key, const char* value) {
    if (std::strcmp(key, "editbuffer") != 0) return;
    const std::vector<uint8_t> bytes = fromHex(value);
    if (bytes.empty()) return;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    synth_.setChunk(bytes.data(), bytes.size());
}

void SQ8LPlugin::sampleRateChanged(double sr) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    synth_.setSampleRate(static_cast<float>(sr));
}

void SQ8LPlugin::run(const float**, float** outputs, uint32_t frames, const MidiEvent* midi, uint32_t midiCount) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // (port) MTS-ESP: one snapshot per block, before the events are dispatched, so the MIDI
    // and voice paths never call into the client.
    mts_.refresh(synth_.mtsEnabled(), synth_.mtsCorrectPitch());
    events_.clear();
    for (uint32_t i = 0; i < midiCount; i++) {
        const MidiEvent& m = midi[i];
        if (m.size == 0 || m.size > MidiEvent::kDataSize) continue;
        sq8l::RawMidiEvent e{};
        e.deltaFrames = static_cast<int32_t>(m.frame);
        for (uint32_t j = 0; j < 3 && j < m.size; j++) e.data[j] = m.data[j];
        events_.push_back(e);
    }
    if (!events_.empty()) synth_.processEvents(events_.data(), static_cast<int32_t>(events_.size()));
    synth_.process(outputs[0], outputs[1], static_cast<int32_t>(frames), true);
}

Plugin* createPlugin() { return new SQ8LPlugin(); }

END_NAMESPACE_DISTRHO
