// SQ8L port: DPF plugin wrapper around the engine (sq8l::Synth).
#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "DistrhoPlugin.hpp"
#include "HostParams.h"
#include "MtsEspTuning.h"
#include "SQ8LVersion.h"
#include "SharedLibrary.h"
#include "Synth.h"

START_NAMESPACE_DISTRHO

class SQ8LPlugin : public Plugin, public Vst2ProgramHooks, public sq8l::MasterNotify {
public:
    SQ8LPlugin();
    ~SQ8LPlugin() override;

    // ---- shared with the editor (DISTRHO_PLUGIN_WANT_DIRECT_ACCESS)
    // Everything that touches the engine outside run() must hold this lock. The audio
    // thread blocks on it, so UI sections must stay short (modal UI releases it).
    std::recursive_mutex& engineMutex() { return mutex_; }
    sq8l::Synth& synth() { return synth_; }
    sq8l::Settings& settings() { return *shared_.settings; }
    // (port) The connected MTS-ESP master's scale name, empty when there is none.
    const std::string& mtsScaleName() const { return mts_.scaleName(); }
    // Editor notifications from the master (CSynth 0x487bb8: WM_APP+3 posted to the form),
    // queued under the engine lock and drained by the editor.
    struct EditorMessage {
        uint32_t wParam;
        int32_t lParam;
    };
    // Copies rather than swaps: a swap would hand the reserved buffer to the editor and
    // leave the audio thread with an empty one to grow again. clear() keeps the capacity.
    std::vector<EditorMessage> takeEditorMessages() {
        std::vector<EditorMessage> out = editorMessages_;
        editorMessages_.clear();
        return out;
    }
    void notifyEditor(int32_t a, int32_t b, int32_t c) override {
        if (editorMessages_.size() >= 64) editorMessages_.erase(editorMessages_.begin());  // editor closed
        editorMessages_.push_back({(static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) & 0xFFFF), c});
    }

    // ---- (port) the sound parameters published to the host (HostParams.h), engine lock held.
    // What the host was last given or told for each: the value it set, or the value the
    // editor reported to it (SQ8LUI::syncParameters). The editor reports every parameter whose
    // edit buffer value differs, so a knob turn or a program change reaches the host.
    int hostParameterValue(uint32_t index) const { return hostValues_[index]; }
    // The editor reports `value` to the host. For a moment the host's values of that parameter
    // are its own reports coming back, older than the knob that is still turning: they are
    // not applied (see setParameterValue).
    void reportedToHost(uint32_t index, int value) {
        hostValues_[index] = value;
        echoUntil_[index] = std::chrono::steady_clock::now() + kEchoTime;
    }

    // ---- VST2 program semantics of the original (CSynth v010/v011/v012/v013/v047)
    void vst2SetProgram(int32_t index) override;
    int32_t vst2GetProgram() override;
    void vst2SetProgramName(const char* name) override;
    void vst2GetProgramName(char* out24) override;
    bool vst2GetProgramNameIndexed(int32_t index, char* out24) override;

protected:
    const char* getLabel() const override { return "SQ8L-X"; }
    const char* getDescription() const override { return "Ensoniq SQ-80 emulation (SQ8L with DAW parameters)"; }
    const char* getMaker() const override { return "Siegfried Kullmann, Claudio Tassitano, Mario Kruselj, GitHub contributors"; }
    const char* getHomePage() const override { return "https://github.com/Poonna/SQ8L-X"; }
    const char* getLicense() const override { return "Free and Open Source"; }
    // (port) This build's number, not the original's 0.91 (SQ8LVersion.h, cmake/Version.cmake).
    uint32_t getVersion() const override {
        return d_version(sq8l::kVersionMajor, sq8l::kVersionMinor, sq8l::kVersionPatch);
    }

    void initParameter(uint32_t index, Parameter& parameter) override;
    float getParameterValue(uint32_t index) const override;
    void setParameterValue(uint32_t index, float value) override;
    void initProgramName(uint32_t index, String& name) override;
    void loadProgram(uint32_t index) override;
    void initState(uint32_t index, State& state) override;
    String getState(const char* key) const override;
    void setState(const char* key, const char* value) override;
    void sampleRateChanged(double sr) override;
    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midi, uint32_t midiCount) override;

private:
    void hostKnowsParameters();  // engine lock held

    // Declared before synth_: the engine notifies (notifyEditor) while it is constructed.
    // Reserved to the cap notifyEditor enforces, in the initialiser rather than the body
    // because a program change arriving in run() reaches notifyEditor on the audio thread,
    // where push_back must not allocate.
    std::vector<EditorMessage> editorMessages_ = [] {
        std::vector<EditorMessage> v;
        v.reserve(64);
        return v;
    }();
    mutable std::recursive_mutex mutex_;
    sq8l::SharedLibrary::Handle shared_;
    // (port) MTS-ESP: declared before synth_, which is handed a pointer to its snapshot.
    sq8l::MtsEspTuning mts_;
    sq8l::Synth synth_;
    std::vector<sq8l::RawMidiEvent> events_;
    std::vector<int> hostValues_;  // see hostParameterValue
    // See reportedToHost. Hosts hand a value back within a block or two; a turning knob is
    // reported every 20 ms, so the window stays open for the whole turn.
    static constexpr std::chrono::milliseconds kEchoTime{250};
    std::vector<std::chrono::steady_clock::time_point> echoUntil_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SQ8LPlugin)
};

END_NAMESPACE_DISTRHO
