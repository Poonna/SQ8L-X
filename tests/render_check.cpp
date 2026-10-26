// Native full-render checker for the C++ engine (no Python, runs on any architecture).
//
//   sq8l_render_check --regression tests/regression
//       Renders the MIDI cases in tests/regression/cases and compares a 64-bit FNV-1a hash
//       of the output with the hash of the ORIGINAL plugin's output (expected.txt).
//       Self-contained: does not need the original DLL.
//   sq8l_render_check tests/golden_raw
//       Compares with full golden renders (tests/export_golden_raw.py, needs the original).
//   sq8l_render_check --polyphony tests/regression
//       Playable voices (EMU -> VOICES and OPTIONS -> Polyphony, port additions): a program
//       without the parameter renders exactly like one asking for the original's 8; every
//       regression case renders identically with 12..64 voices when it never takes over a
//       sounding voice with 8; 1 and 4 voices stay finite; the instance override renders like
//       the same value in the program; then a stress test (40 held notes, 64 voices) on
//       programs of every bank.
//   sq8l_render_check --direction
//       Wave direction (OSC -> DIR, a port addition): a reversed oscillator reads its
//       wavesample backwards (every table index the mirror of the forward one), a reversed
//       one-shot halts where it halts forwards and stays halted across control ticks, a
//       reversed looped wave keeps running, and reversing a whole program changes the sound
//       without breaking it. Needs no files.
//   sq8l_render_check --output-gain tests/regression
//       OPTIONS -> Output gain (port addition, issue #52): every cut scales the uncut
//       render by exactly its power of two, and the cut travels in the host chunk.
//   sq8l_render_check --filter-types
//       Filter types (FILT -> TYPE, a port addition): the response of each type at sample
//       rates from 44.1 to 384 kHz, resonance, fast modulation, smoothing; then programs
//       rendered with every type. Needs no files.
//   sq8l_render_check --filter-fm
//       Audio-rate filter FM (FILT -> FM SRC / FM AMT, a port addition): no FM is the
//       unmodulated filter bit for bit, full-swing FM stays bounded for every type at sample
//       rates from 44.1 to 384 kHz and grows sidebands around a carrier; OFF, amount 0 and a
//       disabled oscillator render like OFF; extreme programs stay finite. Needs no files.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "FilterSQ.h"
#include "FilterSvf.h"
#include "Program.h"
#include "Synth.h"

namespace {

std::vector<uint8_t> readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// Renders a case (golden_raw / regression format); returns L then R, p = end of MIDI data.
// `voices`: playable voices (8 = the original), set in the program (EMU -> VOICES) or, with
// `viaOverride`, as this instance's override (OPTIONS -> Polyphony); both must render the
// same. `steals`: notes that took a sounding voice.
// `cut`: this instance's output cut in dB (OPTIONS -> Output gain), 0 = none.
void render(const std::vector<uint8_t>& buf, size_t& p, std::vector<float>& L, std::vector<float>& R,
            int voices = 8, uint32_t* steals = nullptr, bool viaOverride = false, int cut = 0) {
    auto rd = [&](void* dst, size_t k) {
        std::memcpy(dst, buf.data() + p, k);
        p += k;
    };
    int32_t prog, nblocks;
    rd(&prog, 4);
    rd(&nblocks, 4);
    sq8l::Synth synth(44100.0f);
    synth.setSampleRate(44100.0f);
    synth.setProgram(prog);
    if (viaOverride)
        synth.setPolyphonyOverride(voices);
    else
        synth.editBuffer().current().setU8(sq8l::ofs::Polyphony, unsigned(voices));
    synth.setOutputCut(cut);
    for (int b = 0; b < nblocks; b++) {
        int32_t frames, nev;
        rd(&frames, 4);
        rd(&nev, 4);
        std::vector<sq8l::RawMidiEvent> ev(static_cast<size_t>(nev));
        for (auto& e : ev) {
            rd(&e.deltaFrames, 4);
            uint8_t m[4];
            rd(m, 4);
            e.data[0] = m[0], e.data[1] = m[1], e.data[2] = m[2], e.noteOffVelocity = 0;
        }
        if (nev) synth.processEvents(ev.data(), nev);
        std::vector<float> l(static_cast<size_t>(frames)), r(static_cast<size_t>(frames));
        synth.process(l.data(), r.data(), frames, true);
        L.insert(L.end(), l.begin(), l.end());
        R.insert(R.end(), r.begin(), r.end());
    }
    if (steals) *steals = synth.master().stealCount();
}

uint64_t fnv1a64(const void* data, size_t n, uint64_t h = 0xCBF29CE484222325ull) {
    const auto* b = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 0x100000001B3ull;
    }
    return h;
}

int regression(const std::string& dir) {
    std::ifstream exp(dir + "/expected.txt");
    if (!exp) {
        std::fprintf(stderr, "no %s/expected.txt\n", dir.c_str());
        return 2;
    }
    std::string line;
    int cases = 0, bad = 0;
    long long samples = 0;
    while (std::getline(exp, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream is(line);
        std::string name, hex;
        size_t n = 0;
        is >> name >> n >> hex;
        const std::vector<uint8_t> buf = readAll(dir + "/cases/" + name);
        size_t p = 0;
        std::vector<float> L, R;
        render(buf, p, L, R);
        uint64_t h = fnv1a64(L.data(), L.size() * 4);
        h = fnv1a64(R.data(), R.size() * 4, h);
        char got[17];
        std::snprintf(got, sizeof got, "%016llx", static_cast<unsigned long long>(h));
        cases++;
        samples += static_cast<long long>(L.size());
        if (L.size() != n || hex != got) {
            bad++;
            std::printf("DIFF %s (samples %zu/%zu, hash %s, expected %s)\n", name.c_str(), L.size(), n, got,
                        hex.c_str());
        }
    }
    std::printf("%d/%d regression renders identical to the original (%lld samples per channel)\n", cases - bad,
                cases, samples);
    return bad ? 1 : 0;
}

int golden(const std::string& dir) {
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) {
        std::fprintf(stderr, "no %s\n", dir.c_str());
        return 2;
    }
    int files = 0, bad = 0;
    long long samples = 0;
    for (const std::filesystem::directory_entry& de : it) {
        const std::string name = de.path().filename().string();
        if (name.size() < 5 || name.substr(name.size() - 4) != ".bin") continue;
        const std::vector<uint8_t> buf = readAll(dir + "/" + name);
        size_t p = 0;
        std::vector<float> L, R;
        render(buf, p, L, R);
        const size_t total = L.size();
        const bool same = buf.size() - p == total * 8 && std::memcmp(buf.data() + p, L.data(), total * 4) == 0 &&
                          std::memcmp(buf.data() + p + total * 4, R.data(), total * 4) == 0;
        files++;
        samples += static_cast<long long>(total);
        if (!same) {
            bad++;
            std::printf("DIFF %s\n", name.c_str());
        }
    }
    std::printf("%d/%d renders bit-exact (%lld samples per channel)\n", files - bad, files, samples);
    return bad ? 1 : 0;
}

uint64_t hashLR(const std::vector<float>& L, const std::vector<float>& R) {
    return fnv1a64(R.data(), R.size() * 4, fnv1a64(L.data(), L.size() * 4));
}

bool finite(const std::vector<float>& v) {
    for (float x : v)
        if (!std::isfinite(x)) return false;
    return true;
}

int polyphony(const std::string& dir) {
    std::ifstream exp(dir + "/expected.txt");
    if (!exp) {
        std::fprintf(stderr, "no %s/expected.txt\n", dir.c_str());
        return 2;
    }
    const int more[] = {12, 16, 24, 32, 48, 64};  // more than the original's 8
    const int fewer[] = {1, 4};                   // fewer: these do change the render
    int cases = 0, free = 0, bad = 0, stealing = 0;
    std::string line;
    while (std::getline(exp, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream is(line);
        std::string name;
        is >> name;
        const std::vector<uint8_t> buf = readAll(dir + "/cases/" + name);
        size_t p = 0;
        std::vector<float> L8, R8;
        uint32_t steals = 0;
        render(buf, p, L8, R8, 8, &steals);
        const uint64_t h8 = hashLR(L8, R8);
        cases++;
        if (steals) stealing++;
        else free++;
        {
            // A program of the original (and of every bank file, SysEx import and pre-0.90
            // record) has 0 here: it must play exactly like an explicit 8.
            p = 0;
            std::vector<float> L0, R0;
            render(buf, p, L0, R0, 0);
            if (hashLR(L0, R0) != h8) {
                bad++;
                std::printf("DIFF %s: no VOICES in the program vs an explicit 8\n", name.c_str());
            }
        }
        for (int n : more) {
            p = 0;
            std::vector<float> L, R;
            render(buf, p, L, R, n);
            if (!finite(L) || !finite(R) || L.size() != L8.size()) {
                bad++;
                std::printf("BAD %s with %d voices: non-finite output or wrong length\n", name.c_str(), n);
            } else if (!steals && hashLR(L, R) != h8) {
                bad++;
                std::printf("DIFF %s with %d voices (no voice taken over with 8)\n", name.c_str(), n);
            }
            if (n == 16) {  // the instance's override = the same value in the program
                p = 0;
                std::vector<float> Lo, Ro;
                render(buf, p, Lo, Ro, n, nullptr, true);
                if (hashLR(Lo, Ro) != hashLR(L, R)) {
                    bad++;
                    std::printf("DIFF %s: 16 voices from OPTIONS vs from the program\n", name.c_str());
                }
            }
        }
        for (int n : fewer) {
            p = 0;
            std::vector<float> L, R;
            render(buf, p, L, R, n);
            if (!finite(L) || !finite(R) || L.size() != L8.size()) {
                bad++;
                std::printf("BAD %s with %d voices: non-finite output or wrong length\n", name.c_str(), n);
            }
        }
    }
    std::printf("polyphony: %d cases; %d never take over a voice with 8 and render identically with "
                "12/16/24/32/48/64 voices (%s), %d do (more voices change them, output finite); "
                "no VOICES = 8; 1 and 4 voices finite; OPTIONS override = the program's value\n",
                cases, free, bad ? "FAILED" : "ok", stealing);

    // The host chunk carries the instance's override (OPTIONS -> Polyphony), so every plugin
    // format recalls it, and carries nothing when there is none: that chunk must stay exactly
    // the original's bytes.
    int chunkBad = 0;
    {
        sq8l::Synth a(44100.0f);
        const std::vector<uint8_t> plain = a.getChunk();
        a.setPolyphonyOverride(23);
        const std::vector<uint8_t> with = a.getChunk();
        size_t diff = 0;
        for (size_t i = 0; i < plain.size() && i < with.size(); i++)
            if (plain[i] != with[i]) diff++;
        sq8l::Synth b(44100.0f);
        b.setChunk(with.data(), with.size());
        const int restored = b.polyphonyOverride();
        b.setChunk(plain.data(), plain.size());       // a project without an override
        const int cleared = b.polyphonyOverride();
        if (plain[0x1b] || plain[0x1c] || diff != 2 || restored != 23 || cleared != 0) {
            chunkBad++;
            std::printf("BAD chunk: spare bytes %02x %02x, %zu bytes differ, restored %d, cleared %d\n",
                        plain[0x1b], plain[0x1c], diff, restored, cleared);
        }
        std::printf("chunk: no override = the original's bytes; an override adds %zu bytes and comes "
                    "back as %d; a chunk without one restores \"set by program\" (%s)\n",
                    diff, restored, chunkBad ? "FAILED" : "ok");
    }

    // Stress: 40 held notes with 64 voices, programs from every bank, then the releases
    // (and the same with the original's 8 voices, for the level). With 64 voices every held
    // note must sound: no note takes over another.
    int maxActive = 0, programs = 0, stressBad = 0;
    float peak = 0, peak8 = 0;
    for (int run = 0; run < 2; run++)
    for (int prog = 0; prog < 512; prog += 23) {
        sq8l::Synth synth(44100.0f);
        synth.setSampleRate(44100.0f);
        synth.setProgram(prog);
        synth.editBuffer().current().setU8(sq8l::ofs::Polyphony, run ? 8u : 64u);
        std::vector<float> l(512), r(512);
        std::vector<float> all;
        for (int phase = 0; phase < 2; phase++) {
            std::vector<sq8l::RawMidiEvent> ev(40);
            for (int k = 0; k < 40; k++) {
                ev[size_t(k)].deltaFrames = k * 3;
                ev[size_t(k)].data[0] = phase ? 0x80 : 0x90;
                ev[size_t(k)].data[1] = uint8_t(28 + k);
                ev[size_t(k)].data[2] = phase ? 0 : 100;
                ev[size_t(k)].noteOffVelocity = 0;
            }
            synth.processEvents(ev.data(), 40);
            for (int b = 0; b < 172; b++) {  // 2 s
                synth.process(l.data(), r.data(), 512, true);
                if (phase == 0 && !run) maxActive = std::max(maxActive, int(synth.master().activeVoiceCount()));
                all.insert(all.end(), l.begin(), l.end());
                all.insert(all.end(), r.begin(), r.end());
            }
        }
        if (!run) programs++;
        if (!finite(all)) {
            stressBad++;
            std::printf("BAD stress program %d: non-finite output\n", prog);
        }
        for (float x : all) (run ? peak8 : peak) = std::max(run ? peak8 : peak, std::fabs(x));
    }
    std::printf("stress: %d programs, 40 held notes: up to %d voices sounding with 64, output finite; "
                "peak %.2f (with 8 voices: %.2f), %s\n",
                programs, maxActive, double(peak), double(peak8), stressBad || maxActive != 40 ? "FAILED" : "ok");
    return bad || chunkBad || stressBad || maxActive != 40 ? 1 : 0;
}

// (port, issue #52) OPTIONS -> Output gain: the per-instance cut of the output. Every step is
// a whole power of two, so a cut must scale the uncut render by exactly that factor; the cut
// travels in the host chunk like the polyphony override, and a newly created instance starts
// at the cut SQ8L.ini remembers, which the chunk of a project then overrides.
int outputGain(const std::string& dir) {
    std::ifstream exp(dir + "/expected.txt");
    if (!exp) {
        std::fprintf(stderr, "no %s/expected.txt\n", dir.c_str());
        return 2;
    }
    int cases = 0, bad = 0;
    std::string line;
    while (std::getline(exp, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream is(line);
        std::string name;
        is >> name;
        const std::vector<uint8_t> buf = readAll(dir + "/cases/" + name);
        size_t p = 0;
        std::vector<float> L0, R0;
        render(buf, p, L0, R0);
        cases++;
        for (int db : sq8l::Settings::kOutputCutsDb) {
            if (db == 0) continue;
            const float g = sq8l::Settings::outputGainFor(db);
            p = 0;
            std::vector<float> L, R;
            render(buf, p, L, R, 8, nullptr, false, db);
            if (L.size() != L0.size() || R.size() != R0.size()) {
                bad++;
                std::printf("BAD %s at -%d dB: wrong length\n", name.c_str(), db);
                continue;
            }
            // Exact below the top of the subnormal range: scaling by a power of two only
            // changes the exponent, down to where a sample would lose its last bits.
            size_t off = 0;
            for (size_t i = 0; i < L0.size() && !off; i++)
                for (const auto& pair : {std::pair<float, float>{L0[i], L[i]}, {R0[i], R[i]}}) {
                    const float want = pair.first * g;
                    if (std::fabs(want) >= 1e-30f ? pair.second != want : std::fabs(pair.second) > 1e-30f)
                        off = i + 1;
                }
            if (off) {
                bad++;
                std::printf("DIFF %s at -%d dB: sample %zu is not the uncut render times %g\n",
                            name.c_str(), db, off - 1, double(g));
            }
        }
    }
    std::printf("output gain: %d cases, every cut (-6/-12/-18/-24 dB) scales the uncut render "
                "exactly (%s)\n", cases, bad ? "FAILED" : "ok");

    // The chunk carries the cut, and carries nothing when there is none: such a chunk must
    // stay exactly the original's bytes.
    int chunkBad = 0;
    {
        sq8l::Synth a(44100.0f);
        const std::vector<uint8_t> plain = a.getChunk();
        a.setOutputCut(18);
        const std::vector<uint8_t> with = a.getChunk();
        size_t diff = 0;
        for (size_t i = 0; i < plain.size() && i < with.size(); i++)
            if (plain[i] != with[i]) diff++;
        sq8l::Synth b(44100.0f);
        b.setChunk(with.data(), with.size());
        const int restored = b.outputCut();
        b.setChunk(plain.data(), plain.size());  // a project saved without a cut
        const int cleared = b.outputCut();
        // A step this build does not know (a chunk from a later version) reads as no cut.
        std::vector<uint8_t> odd = with;
        odd[0x1e] = 9;
        b.setChunk(odd.data(), odd.size());
        const int unknown = b.outputCut();
        if (plain[0x1e] || diff != 1 || restored != 18 || cleared != 0 || unknown != 0) {
            chunkBad++;
            std::printf("BAD chunk: spare byte %02x, %zu bytes differ, restored %d, cleared %d, "
                        "unknown step %d\n", plain[0x1e], diff, restored, cleared, unknown);
        }
        // Where a newly created instance starts: the cut SQ8L.ini remembers, and no cut when
        // the key holds a step that is not on offer. A chunk always wins over it.
        sq8l::Settings cfg;
        cfg.port[sq8l::Settings::kPortOutputCut] = 12;
        const int fromIni = sq8l::Synth(44100.0f, nullptr, &cfg).outputCut();
        cfg.port[sq8l::Settings::kPortOutputCut] = 7;
        const int fromBadIni = sq8l::Synth(44100.0f, nullptr, &cfg).outputCut();
        cfg.port[sq8l::Settings::kPortOutputCut] = 12;
        sq8l::Synth c(44100.0f, nullptr, &cfg);
        c.setChunk(plain.data(), plain.size());
        const int chunkWins = c.outputCut();
        if (fromIni != 12 || fromBadIni != 0 || chunkWins != 0) {
            chunkBad++;
            std::printf("BAD start: from the ini %d, from a bad ini %d, chunk over the ini %d\n",
                        fromIni, fromBadIni, chunkWins);
        }
        std::printf("chunk: no cut = the original's bytes; a cut adds %zu byte and comes back as "
                    "-%d dB; a chunk without one, or with an unknown step, is no cut; a new "
                    "instance starts at the ini's cut and a chunk overrides it (%s)\n",
                    diff, restored, chunkBad ? "FAILED" : "ok");
    }
    return bad || chunkBad ? 1 : 0;
}

// (port) Wave direction (OSC -> DIR, ofs::Osc::Dir): the oscillator reads its wavesample
// backwards. A looped wave fills its table, so with the same note and frequency the index a
// reversed oscillator reads at step k is the exact mirror of the forward one:
//
//   forward acc = k*freq                 -> index  a           (k*freq = a << shift | b)
//   reverse acc = (N << shift) - 1 - k*freq -> index  N - 1 - a
//
// so the two indices sum to N-1 at every step until the first wrap. That is checked here
// sample by sample, plus the inertness of the parameter and what a whole program renders.
int direction() {
    int bad = 0;
    constexpr uint32_t kSteps = 2000;

    // The DOC clocked at its own rate: one render() is exactly one oscillator step.
    auto indices = [](int wave, bool reverse, std::vector<uint32_t>& idx, uint32_t& tableSize) {
        sq8l::Doc d(1, 1);
        d.setSampleRate(sq8l::Doc::kDocRate);
        d.setOscReverse(0, 0, reverse);
        sq8l::DocVoiceParams* p = d.params(0);
        for (int i = 0; i < 3; i++) p->osc[i] = sq8l::DocOscParams{};
        p->osc[0].enabled = -1;
        p->osc[0].wave = wave;
        p->osc[0].level = 0x7f00;
        d.startVoice(0, 60, -1, -1, -1);
        d.update(0);
        const sq8l::DocOsc& o = d.voice[0].osc[0];
        tableSize = static_cast<uint32_t>(o.tableSize);
        idx.clear();
        for (uint32_t k = 0; k < kSteps; k++) {
            idx.push_back(o.acc >> (o.shift & 31));
            d.render(0);
            if (o.halted != 0) break;
        }
    };

    for (int wave : {0, 2, 20, 33, 44}) {  // looped waves (< 54): the table is the whole loop
        std::vector<uint32_t> f, r;
        uint32_t nf = 0, nr = 0;
        indices(wave, false, f, nf);
        indices(wave, true, r, nr);
        const size_t n = std::min(f.size(), r.size());
        size_t mismatch = 0;
        for (size_t k = 0; k < n; k++)
            if (f[k] + r[k] != nf - 1) mismatch++;
        const bool moved = n > 1 && f[1] != f[0] && r[1] != r[0];
        if (nf != nr || n < kSteps || mismatch || !moved) {
            bad++;
            std::printf("BAD wave %d: table %u/%u, %zu steps, %zu not mirrored, moved %d\n", wave, nf, nr, n,
                        mismatch, int(moved));
        }
    }
    std::printf("direction: 5 looped waves, %u steps each, every reversed table index the mirror of the "
                "forward one (%s)\n",
                kSteps, bad ? "FAILED" : "ok");

    // One-shot waves (>= 54) must stop reversed exactly as they stop forwards, and STAY
    // stopped: update() runs every control tick and clears `halted` unconditionally, so a
    // one-shot only stays dead because its accumulator is parked on the terminator and keeps
    // reading that zero. Reverse used to wrap back to live data instead, and the voice played
    // the wave backwards again on every tick -- a one-shot turned into a loop.
    int oneShotBad = 0;
    auto haltRun = [](int wave, bool reverse, int& haltStep, int& movedAfter) {
        sq8l::Doc d(1, 1);
        d.setSampleRate(sq8l::Doc::kDocRate);
        d.setOscReverse(0, 0, reverse);
        sq8l::DocVoiceParams* p = d.params(0);
        for (int i = 0; i < 3; i++) p->osc[i] = sq8l::DocOscParams{};
        p->osc[0].enabled = -1;
        p->osc[0].wave = wave;
        p->osc[0].level = 0x7f00;
        d.startVoice(0, 60, -1, -1, -1);
        d.update(0);
        const sq8l::DocOsc& o = d.voice[0].osc[0];
        haltStep = -1;
        movedAfter = 0;
        uint32_t prev = o.acc;
        for (int k = 0; k < 120000; k++) {
            d.render(0);
            if (o.halted != 0 && haltStep < 0) haltStep = k;
            if (haltStep >= 0 && o.acc != prev) movedAfter++;
            prev = o.acc;
            if ((k % 64) == 63) d.update(0);  // the control tick the master runs
        }
    };
    for (int wave = 54; wave < 75; wave++) {
        int hf = 0, hr = 0, mf = 0, mr = 0;
        haltRun(wave, false, hf, mf);
        haltRun(wave, true, hr, mr);
        // the same length within a step (the wrapped position costs forward one extra sample)
        if (hf < 0 || hr < 0 || std::abs(hf - hr) > 1 || mf != 0 || mr != 0) {
            oneShotBad++;
            std::printf("BAD one-shot wave %d: halt fwd %d rev %d, moved after halt fwd %d rev %d\n", wave, hf, hr,
                        mf, mr);
        }
    }
    std::printf("one-shot: waves 54-74 reversed halt where they halt forwards and stay halted across "
                "control ticks (%s)\n",
                oneShotBad ? "FAILED" : "ok");

    // A looped wave has no terminator, so reverse keeps going, as it does forwards.
    int loopBad = 0;
    for (int wave : {0, 2, 20, 44, 53}) {
        int h = 0, m = 0;
        haltRun(wave, true, h, m);
        if (h >= 0) {
            loopBad++;
            std::printf("BAD looped wave %d reversed: halted at step %d\n", wave, h);
        }
    }
    std::printf("looped: 5 waves reversed keep running, as they do forwards (%s)\n", loopBad ? "FAILED" : "ok");
    oneShotBad += loopBad;

    // A whole program: the byte the original never writes leaves the render untouched, and
    // turning it on changes the sound without breaking it.
    int progBad = 0;
    auto renderProg = [](int prog, int dir, std::vector<float>& L, std::vector<float>& R) {
        sq8l::Synth synth(44100.0f);
        synth.setSampleRate(44100.0f);
        synth.setProgram(prog);
        for (int i = 0; i < 3; i++)
            synth.editBuffer().current().setU8(sq8l::ofs::osc(i) + sq8l::ofs::Osc::Dir, uint8_t(dir));
        std::vector<sq8l::RawMidiEvent> ev(1);
        ev[0].deltaFrames = 0;
        ev[0].data[0] = 0x90;
        ev[0].data[1] = 60;
        ev[0].data[2] = 100;
        ev[0].noteOffVelocity = 0;
        synth.processEvents(ev.data(), 1);
        std::vector<float> l(512), r(512);
        for (int b = 0; b < 86; b++) {  // 1 s
            synth.process(l.data(), r.data(), 512, true);
            L.insert(L.end(), l.begin(), l.end());
            R.insert(R.end(), r.begin(), r.end());
        }
    };
    int changed = 0, programs = 0;
    for (int prog = 0; prog < 512; prog += 23) {
        std::vector<float> L0, R0, L1, R1;
        renderProg(prog, 0, L0, R0);
        renderProg(prog, 1, L1, R1);
        programs++;
        if (!finite(L1) || !finite(R1) || L1.size() != L0.size()) {
            progBad++;
            std::printf("BAD program %d reversed: non-finite output or wrong length\n", prog);
        }
        if (hashLR(L0, R0) != hashLR(L1, R1)) changed++;
    }
    if (changed == 0) {
        progBad++;
        std::printf("BAD: reversing every oscillator changed no program\n");
    }
    std::printf("programs: %d rendered, %d sound different reversed, output finite (%s)\n", programs, changed,
                progBad ? "FAILED" : "ok");
    return bad || oneShotBad || progBad ? 1 : 0;
}

// (port) Filter types (FILT -> TYPE, ofs::Filter::Type, FilterSvf). The filter alone, at
// sample rates from 44.1 to 384 kHz: each type passes and stops what it should a decade
// either side of its cutoff, resonance raises the peak (and narrows the band), random fast
// modulation of cutoff and resonance stays finite and bounded, and a cutoff step is
// smoothed. Then whole programs: no factory program has the byte set, an out-of-range value
// renders like type 0, every other type renders finite and non-silent and changes the
// sound, HP2 is brighter than LP2 and LP4 darker, and switching the type while a note plays
// stays finite. Needs no files.
int filterTypes() {
    using namespace sq8l;
    constexpr double kPi = 3.14159265358979323846;
    const char* const names[FilterType::Count] = {"SQ-80", "LP2", "BP2", "HP2", "NOTCH", "LP4"};
    int bad = 0;
    auto fail = [&](const char* what, int type, double sr, double v) {
        bad++;
        std::printf("BAD %s %s at %.0f Hz: %g\n", names[type], what, sr, v);
    };

    // The cutoff index nearest 1 kHz.
    const float* hz = filterUnitTables().cutoffHz;
    int c1k = 0;
    for (int c = 0; c < 256; c++)
        if (std::fabs(hz[c] - 1000.0f) < std::fabs(hz[c1k] - 1000.0f)) c1k = c;
    const double fc = hz[c1k];
    // Steady-state gain of a sine at f (the second half of 1 s).
    auto gain = [&](const SvfTable& t, float sr, int type, int reso, double f) {
        FilterSvf flt;
        flt.init(&t, sr);
        flt.setType(type);
        flt.setParams(c1k, reso, true);
        const int n = static_cast<int>(sr);
        double in2 = 0, out2 = 0;
        for (int i = 0; i < n; i++) {
            const double x = std::sin(2 * kPi * f * i / sr);
            const double y = flt.process(x);
            if (i >= n / 2) in2 += x * x, out2 += y * y;
        }
        return std::sqrt(out2 / in2);
    };
    uint32_t rng = 0x12345678u;
    auto next = [&rng] {
        rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
        return rng;
    };
    for (const float sr : {44100.0f, 48000.0f, 88200.0f, 96000.0f, 192000.0f, 384000.0f}) {
        SvfTable t;
        t.compute(sr);
        double lo[FilterType::Count] = {}, mid[FilterType::Count] = {}, hi[FilterType::Count] = {};
        for (int type = 1; type < FilterType::Count; type++) {
            lo[type] = gain(t, sr, type, 0, fc / 10);
            mid[type] = gain(t, sr, type, 0, fc);
            hi[type] = gain(t, sr, type, 0, fc * 10);
        }
        using namespace FilterType;
        // RES 0: Butterworth lowpass/highpass (-3 dB at the cutoff, 12 or 24 dB/oct), unity
        // peak bandpass, notch at the cutoff.
        if (lo[Lp2] < 0.95) fail("passband (cutoff/10)", Lp2, sr, lo[Lp2]);
        if (hi[Lp2] > 0.02) fail("stopband (cutoff*10)", Lp2, sr, hi[Lp2]);
        if (mid[Lp2] < 0.65 || mid[Lp2] > 0.76) fail("gain at the cutoff", Lp2, sr, mid[Lp2]);
        if (lo[Lp4] < 0.95) fail("passband (cutoff/10)", Lp4, sr, lo[Lp4]);
        if (hi[Lp4] > 0.0005) fail("stopband (cutoff*10)", Lp4, sr, hi[Lp4]);
        if (mid[Lp4] < 0.65 || mid[Lp4] > 0.76) fail("gain at the cutoff", Lp4, sr, mid[Lp4]);
        if (hi[Hp2] < 0.95) fail("passband (cutoff*10)", Hp2, sr, hi[Hp2]);
        if (lo[Hp2] > 0.02) fail("stopband (cutoff/10)", Hp2, sr, lo[Hp2]);
        if (mid[Hp2] < 0.65 || mid[Hp2] > 0.76) fail("gain at the cutoff", Hp2, sr, mid[Hp2]);
        if (mid[Bp2] < 0.98 || mid[Bp2] > 1.02) fail("gain at the cutoff", Bp2, sr, mid[Bp2]);
        if (lo[Bp2] > 0.2 || hi[Bp2] > 0.2) fail("skirts (cutoff/10, *10)", Bp2, sr, std::max(lo[Bp2], hi[Bp2]));
        if (mid[Notch] > 0.01) fail("gain at the cutoff", Notch, sr, mid[Notch]);
        if (lo[Notch] < 0.97 || hi[Notch] < 0.97) fail("passband", Notch, sr, std::min(lo[Notch], hi[Notch]));
        // RES 31: the lowpass and highpass peak at the cutoff, the bandpass narrows.
        for (const int type : {Lp2, Hp2, Lp4}) {
            const double g = gain(t, sr, type, 31, fc);
            if (!(g > 2.0 && g < 10.0)) fail("resonant peak (RES 31)", type, sr, g);
        }
        const double narrow = gain(t, sr, Bp2, 31, fc / 10);
        if (narrow > lo[Bp2] / 4) fail("band at RES 31, gain at cutoff/10", Bp2, sr, narrow);
        const double notch = gain(t, sr, Notch, 31, fc);
        if (notch > 0.01) fail("gain at the cutoff, RES 31", Notch, sr, notch);

        // Fast random modulation (a jump every 16 samples, half of them unsmoothed) on white
        // noise: always finite, never more than a resonant peak.
        for (int type = 1; type < Count; type++) {
            FilterSvf flt;
            flt.init(&t, sr);
            flt.setType(type);
            double peak = 0;
            for (int i = 0; i < static_cast<int>(sr); i++) {
                if (i % 16 == 0) {
                    const int32_t c = int32_t(next() % 256), r = int32_t(next() % 32);
                    flt.setParams(c, r, (next() & 1) != 0);
                }
                const double x = (next() / 4294967295.0) * 2 - 1;
                const double y = flt.process(x);
                if (!std::isfinite(y)) {
                    peak = INFINITY;
                    break;
                }
                peak = std::max(peak, std::fabs(y));
            }
            if (!(peak < 50)) fail("under fast modulation, peak", type, sr, peak);
        }

        // A cutoff step from index 0 to 255 under a 2 kHz sine: unsmoothed the sine is through
        // within a few samples, smoothed it is not yet, and both settle to the same output.
        for (const int type : {Lp2, Lp4}) {
            double first[2] = {}, last[2] = {};
            for (int smooth = 0; smooth < 2; smooth++) {
                FilterSvf flt;
                flt.init(&t, sr);
                flt.setType(type);
                flt.setParams(0, 0, true);
                const int settle = static_cast<int>(sr / 2), after = static_cast<int>(sr / 10);
                for (int i = 0; i < settle + after; i++) {
                    if (i == settle) flt.setParams(255, 0, smooth == 0);
                    const double y = flt.process(std::sin(2 * kPi * 2000 * i / sr));
                    if (i >= settle && i < settle + 10) first[smooth] = std::max(first[smooth], std::fabs(y));
                    if (i == settle + after - 1) last[smooth] = y;
                }
            }
            if (!(first[1] < first[0] / 4)) fail("cutoff step not smoothed (first 10 samples)", type, sr, first[1]);
            if (std::fabs(last[0] - last[1]) > 1e-6) fail("smoothed step does not settle", type, sr, last[1]);
        }
    }
    std::printf("filter: 5 types at 6 sample rates: pass/stop bands, resonance, fast modulation, smoothing (%s)\n",
                bad ? "FAILED" : "ok");

    // Whole programs.
    int progBad = 0, set = 0;
    {
        Synth synth(44100.0f);
        for (int prog = 0; prog < 512; prog++) {
            synth.setProgram(prog);
            if (synth.editBuffer().current().u8(ofs::filter(0) + ofs::Filter::Type) != 0) set++;
        }
    }
    if (set) {
        progBad++;
        std::printf("BAD: %d factory programs have the filter type byte set\n", set);
    }
    // `prog`: a factory program, -1 = INIT (OSC1 SAW, no filter modulation or tracking).
    // `types`: the type byte for each block of the 1 s note (86 blocks of 512). `freq`: the
    // filter's FREQ instead of the program's, -1 = the program's.
    auto renderProg = [](int prog, const std::vector<int>& types, std::vector<float>& L, std::vector<float>& R,
                         int freq = -1) {
        Synth synth(44100.0f);
        synth.setSampleRate(44100.0f);
        synth.setProgram(prog < 0 ? 0 : prog);
        if (prog < 0) initProgram(synth.editBuffer().current());
        if (freq >= 0) synth.editBuffer().current().setU8(ofs::filter(0) + ofs::Filter::Freq, unsigned(freq));
        std::vector<RawMidiEvent> ev(1);
        ev[0].deltaFrames = 0;
        ev[0].data[0] = 0x90;
        ev[0].data[1] = 60;
        ev[0].data[2] = 100;
        ev[0].noteOffVelocity = 0;
        synth.processEvents(ev.data(), 1);
        std::vector<float> l(512), r(512);
        for (int b = 0; b < 86; b++) {
            synth.editBuffer().current().setU8(ofs::filter(0) + ofs::Filter::Type, unsigned(types[size_t(b)]));
            synth.process(l.data(), r.data(), 512, true);
            L.insert(L.end(), l.begin(), l.end());
            R.insert(R.end(), r.begin(), r.end());
        }
    };
    auto rms = [](const std::vector<float>& v) {
        double s = 0;
        for (const float x : v) s += double(x) * x;
        return std::sqrt(s / double(v.size()));
    };
    // Share of the signal's energy in its first difference: higher is brighter.
    auto brightness = [](const std::vector<float>& v) {
        double d = 0, s = 0;
        for (size_t i = 1; i < v.size(); i++) {
            const double e = double(v[i]) - v[i - 1];
            d += e * e;
            s += double(v[i]) * v[i];
        }
        return s > 0 ? d / s : 0.0;
    };

    // A sawtooth (INIT) through each type with the cutoff in the middle (FREQ 64, ~2.2 kHz,
    // a C4 note): every type audible, and LP4 darker than LP2, darker than BP2, darker than
    // HP2.
    {
        double level[FilterType::Count], bright[FilterType::Count];
        for (int type = 0; type < FilterType::Count; type++) {
            std::vector<float> L, R;
            renderProg(-1, std::vector<int>(86, type), L, R, 64);
            level[type] = rms(L);
            bright[type] = brightness(L);
            std::printf("  INIT saw, FREQ 64, %-5s: rms %.4f, brightness %.4f\n", names[type], level[type], bright[type]);
            if (!finite(L) || !finite(R) || level[type] < 1e-3) {
                progBad++;
                std::printf("BAD INIT %s: non-finite or silent\n", names[type]);
            }
        }
        using namespace FilterType;
        if (!(bright[Lp4] < bright[Lp2] && bright[Lp2] < bright[Bp2] && bright[Bp2] < bright[Hp2])) {
            progBad++;
            std::printf("BAD INIT: brightness not LP4 < LP2 < BP2 < HP2\n");
        }
    }

    // The factory programs as they are (many keep the filter wide open, where a highpass or
    // bandpass rightly removes almost everything, so nothing is asked of their level): every
    // type finite and different from type 0, a type byte no version writes plays type 0, and
    // switching the type while a note plays stays finite.
    int programs = 0;
    int changed[FilterType::Count] = {};
    for (int prog = 0; prog < 512; prog += 23) {
        std::vector<float> L[FilterType::Count], R[FilterType::Count];
        for (int type = 0; type < FilterType::Count; type++)
            renderProg(prog, std::vector<int>(86, type), L[type], R[type]);
        if (rms(L[0]) < 1e-4) continue;  // nothing to filter
        programs++;
        std::vector<float> L200, R200;
        renderProg(prog, std::vector<int>(86, 200), L200, R200);
        if (hashLR(L200, R200) != hashLR(L[0], R[0])) {
            progBad++;
            std::printf("BAD program %d: type byte 200 does not render like type 0\n", prog);
        }
        for (int type = 1; type < FilterType::Count; type++) {
            if (!finite(L[type]) || !finite(R[type])) {
                progBad++;
                std::printf("BAD program %d %s: non-finite output\n", prog, names[type]);
            }
            if (hashLR(L[type], R[type]) != hashLR(L[0], R[0])) changed[type]++;
        }
        // SQ-80 -> LP4 -> BP2 -> SQ-80
        std::vector<int> seq(86, FilterType::Sq80);
        for (int b = 20; b < 40; b++) seq[size_t(b)] = FilterType::Lp4;
        for (int b = 40; b < 60; b++) seq[size_t(b)] = FilterType::Bp2;
        std::vector<float> Ls, Rs;
        renderProg(prog, seq, Ls, Rs);
        if (!finite(Ls) || !finite(Rs)) {
            progBad++;
            std::printf("BAD program %d: non-finite output switching types mid-note\n", prog);
        }
    }
    std::printf("programs: %d rendered, %d factory programs with the type set, byte 200 renders as type 0\n",
                programs, set);
    for (int type = 1; type < FilterType::Count; type++) {
        std::printf("  %-5s changes the sound of %d/%d\n", names[type], changed[type], programs);
        if (changed[type] < programs) progBad++;
    }
    std::printf("programs (%s)\n", progBad ? "FAILED" : "ok");
    return bad || progBad ? 1 : 0;
}

// Audio-rate filter FM (FILT -> FM SRC / FM AMT, a port addition). The filters alone: the
// SQ-80 table's pole and feedback run monotonically with the cutoff (what processFm's clamp
// relies on); processFm with no FM is process() bit for bit, for every type; full-swing FM
// at every rate up to Nyquist, on noise, at RES 0 and 31, stays finite and bounded at sample
// rates from 44.1 to 384 kHz; a sine through a filter whose cutoff a slower sine moves grows
// sidebands at the carrier +- the modulator, which are not there without FM. Then whole
// programs: no factory program has the bytes set, a source byte no version writes, an amount
// of 0 and an oscillator whose OUTPUT is off render exactly like OFF; every type with every
// source at +-63 and RES 31 renders finite at 44.1, 96 and 192 kHz; every voice of a chord
// takes the setting; and FM from a silent oscillator audibly changes the sound. Needs no
// files.
int filterFm() {
    using namespace sq8l;
    constexpr double kPi = 3.14159265358979323846;
    const char* const names[FilterType::Count] = {"SQ-80", "LP2", "BP2", "HP2", "NOTCH", "LP4"};
    int bad = 0;
    uint32_t rng = 0x9e3779b9u;
    auto next = [&rng] {
        rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
        return rng;
    };
    auto noise = [&next] { return (next() / 4294967295.0) * 2 - 1; };
    double worstPeak[FilterType::Count] = {};

    // One filter of either kind behind one interface.
    struct Flt {
        FilterSQ sq;
        FilterSvf svf;
        int type;
        Flt(const FilterTable* tq, const SvfTable* ts, float sr, int t) : type(t) {
            sq.init(tq, sr);
            svf.init(ts, sr);
            svf.setType(t);
        }
        void set(int32_t c, int32_t r, bool imm) { type == 0 ? sq.setParams(c, r, imm) : svf.setParams(c, r, imm); }
        double run(double x) { return type == 0 ? sq.process(x) : svf.process(x); }
        double runFm(double x, double fm) { return type == 0 ? sq.processFm(x, fm) : svf.processFm(x, fm); }
    };

    for (const float sr : {44100.0f, 48000.0f, 96000.0f, 192000.0f, 384000.0f}) {
        auto tq = std::make_unique<FilterTable>();
        tq->compute(sr);
        SvfTable ts;
        ts.compute(sr);
        for (int r = 0; r < 32; r++) {
            const float(*col)[2] = tq->entry[r];
            int dirP = 0, dirK = 0;
            bool mono = true;
            for (int c = 1; c < 256; c++) {
                const float dp = col[c][0] - col[c - 1][0], dk = col[c][1] - col[c - 1][1];
                const int sp = dp > 0 ? 1 : (dp < 0 ? -1 : 0), sk = dk > 0 ? 1 : (dk < 0 ? -1 : 0);
                if (sp && dirP && sp != dirP) mono = false;
                if (sk && dirK && sk != dirK) mono = false;
                if (sp) dirP = sp;
                if (sk) dirK = sk;
            }
            if (!mono) {
                bad++;
                std::printf("BAD SQ-80 table at %.0f Hz, RES %d: pole or feedback not monotonic in the cutoff\n", sr, r);
            }
        }
        for (int type = 0; type < FilterType::Count; type++) {
            // No FM: bit for bit the unmodulated filter, through random parameter jumps, at a
            // quarter of full scale (full-scale noise through unsmoothed jumps can now and then
            // take the SQ-80 ladder past processFm's soft limit, see FilterSQ.cpp).
            {
                Flt a(tq.get(), &ts, sr, type), b(tq.get(), &ts, sr, type);
                bool same = true;
                for (int i = 0; i < 20000 && same; i++) {
                    if (i % 300 == 0) {
                        const int32_t c = int32_t(next() % 256), r = int32_t(next() % 32);
                        const bool imm = (next() & 1) != 0;
                        a.set(c, r, imm);
                        b.set(c, r, imm);
                    }
                    const double x = noise() * 0.25;
                    const double ya = a.run(x), yb = b.runFm(x, 0.0);
                    same = std::memcmp(&ya, &yb, sizeof ya) == 0;
                }
                if (!same) {
                    bad++;
                    std::printf("BAD %s at %.0f Hz: processFm(x, 0) differs from process(x)\n", names[type], sr);
                }
            }
            // Full swing (+-126 index steps, FM AMT +-63) as a square wave at modulator rates up
            // to Nyquist, and as per-sample random FM, on noise: finite and bounded.
            for (const int reso : {0, 31}) {
                double peak = 0;
                for (const double fmod : {5.0, 200.0, 3000.0, sr * 0.25, sr * 0.5, -1.0}) {
                    for (const int cut : {0, 128, 255}) {
                        Flt f(tq.get(), &ts, sr, type);
                        f.set(cut, reso, true);
                        for (int i = 0; i < static_cast<int>(sr / 4); i++) {
                            const double m = fmod < 0 ? noise() : (std::sin(2 * kPi * fmod * i / sr) >= 0 ? 1.0 : -1.0);
                            const double y = f.runFm(noise(), 126.0 * m);
                            if (!std::isfinite(y)) {
                                peak = INFINITY;
                                break;
                            }
                            peak = std::max(peak, std::fabs(y));
                        }
                    }
                }
                worstPeak[type] = std::max(worstPeak[type], peak);
                if (!(peak < 50)) {
                    bad++;
                    std::printf("BAD %s at %.0f Hz, RES %d: full-swing FM peak %g\n", names[type], sr, reso, peak);
                }
            }
        }
    }
    std::printf("filter FM: 6 types at 5 sample rates: table monotonic, no FM = process(), full swing bounded (%s)\n",
                bad ? "FAILED" : "ok");
    for (int type = 0; type < FilterType::Count; type++)
        std::printf("  %-5s worst full-swing peak on full-scale noise: %.2f\n", names[type], worstPeak[type]);

    // Sidebands: a sine carrier at 1.3 times the cutoff, the cutoff moved by a 137 Hz sine
    // (+-30 index steps), at 48 kHz. Goertzel power at the carrier and at carrier +- 137 Hz
    // over the second half of 1 s (Hann window).
    {
        const float sr = 48000.0f;
        auto tq = std::make_unique<FilterTable>();
        tq->compute(sr);
        SvfTable ts;
        ts.compute(sr);
        const float* hz = filterUnitTables().cutoffHz;
        const int cut = 128;
        const double fc = hz[cut] * 1.3, fm = 137.0;
        auto power = [&](const std::vector<double>& y, double f) {
            const size_t n = y.size();
            double re = 0, im = 0;
            for (size_t i = 0; i < n; i++) {
                const double w = 0.5 - 0.5 * std::cos(2 * kPi * double(i) / double(n - 1));
                re += w * y[i] * std::cos(2 * kPi * f * double(i) / sr);
                im -= w * y[i] * std::sin(2 * kPi * f * double(i) / sr);
            }
            return re * re + im * im;
        };
        for (int type = 0; type < FilterType::Count; type++) {
            double side[2] = {}, carrier[2] = {};
            for (int on = 0; on < 2; on++) {
                Flt f(tq.get(), &ts, sr, type);
                f.set(cut, 8, true);
                std::vector<double> y;
                const int n = static_cast<int>(sr);
                for (int i = 0; i < n; i++) {
                    const double x = std::sin(2 * kPi * fc * i / sr);
                    const double m = on ? 30.0 * std::sin(2 * kPi * fm * i / sr) : 0.0;
                    const double v = f.runFm(x, m);
                    if (i >= n / 2) y.push_back(v);
                }
                carrier[on] = power(y, fc);
                side[on] = power(y, fc - fm) + power(y, fc + fm);
            }
            const double offDb = 10 * std::log10(side[0] / carrier[0] + 1e-300);
            const double onDb = 10 * std::log10(side[1] / carrier[1] + 1e-300);
            std::printf("  %-5s sidebands re carrier: OFF %7.1f dB, FM %6.1f dB\n", names[type], offDb, onDb);
            if (!(onDb > -30 && onDb > offDb + 40)) {
                bad++;
                std::printf("BAD %s: FM grows no sidebands\n", names[type]);
            }
        }
    }

    // Whole programs.
    int progBad = 0;
    auto failP = [&progBad](const std::string& what) {
        progBad++;
        std::printf("BAD %s\n", what.c_str());
    };
    {
        Synth synth(44100.0f);
        int set = 0;
        for (int prog = 0; prog < 512; prog++) {
            synth.setProgram(prog);
            const Program& p = synth.editBuffer().current();
            if (p.u8(ofs::FilterFmSource) != 0 || p.u8(ofs::FilterFmAmount) != 0) set++;
        }
        if (set) failP(std::to_string(set) + " factory programs have a filter FM byte set");
    }
    struct Setup {
        int prog = -1;  // a factory program; -1 = INIT with all three oscillators on (below)
        bool plainInit = false;  // INIT as it is: OSC2 and OSC3 OUTPUT off
        int type = 0, src = 0, amt = 0, res = -1, freq = -1;
        std::vector<int> keys{60};
        std::vector<int> srcPerBlock;  // FM SRC per block of 512, empty = `src` throughout
    };
    // 1 s of the notes, held. `voicesFm`: the filter FM source every sounding voice reports at
    // the end, -2 if they disagree.
    auto render = [](float sr, const Setup& s, std::vector<float>& L, std::vector<float>& R, int* voicesFm = nullptr) {
        Synth synth(sr);
        synth.setSampleRate(sr);
        synth.setProgram(s.prog < 0 ? 0 : s.prog);
        Program& p = synth.editBuffer().current();
        if (s.prog < 0) {
            initProgram(p);
            p.setS8(ofs::filter(0) + ofs::Filter::Freq, 50);
            if (!s.plainInit) {
                // OSC1's saw heard; OSC2 a fifth up and OSC3 an octave and a tritone up, both
                // ON at LEVEL 0, so they modulate the filter without being heard.
                p.setS8(ofs::osc(1) + ofs::Osc::Enable, 1);
                p.setS8(ofs::osc(1) + ofs::Osc::Level, 0);
                p.setS8(ofs::osc(1) + ofs::Osc::Semi, 7);
                p.setS8(ofs::osc(2) + ofs::Osc::Enable, 1);
                p.setS8(ofs::osc(2) + ofs::Osc::Level, 0);
                p.setS8(ofs::osc(2) + ofs::Osc::Oct, 1);
                p.setS8(ofs::osc(2) + ofs::Osc::Semi, 6);
            }
        }
        p.setU8(ofs::filter(0) + ofs::Filter::Type, unsigned(s.type));
        if (s.res >= 0) p.setS8(ofs::filter(0) + ofs::Filter::Res, s.res);
        if (s.freq >= 0) p.setS8(ofs::filter(0) + ofs::Filter::Freq, s.freq);
        p.setU8(ofs::FilterFmSource, unsigned(s.src));
        p.setS8(ofs::FilterFmAmount, s.amt);
        std::vector<RawMidiEvent> ev(s.keys.size());
        for (size_t i = 0; i < ev.size(); i++) {
            ev[i].deltaFrames = 0;
            ev[i].data[0] = 0x90;
            ev[i].data[1] = uint8_t(s.keys[i]);
            ev[i].data[2] = 100;
            ev[i].noteOffVelocity = 0;
        }
        synth.processEvents(ev.data(), int32_t(ev.size()));
        std::vector<float> l(512), r(512);
        const int blocks = int(sr) / 512;
        for (int b = 0; b < blocks; b++) {
            if (!s.srcPerBlock.empty())
                p.setU8(ofs::FilterFmSource, unsigned(s.srcPerBlock[size_t(b) % s.srcPerBlock.size()]));
            synth.process(l.data(), r.data(), 512, true);
            L.insert(L.end(), l.begin(), l.end());
            R.insert(R.end(), r.begin(), r.end());
        }
        if (voicesFm) {
            *voicesFm = -1000;
            for (int v = 0; v < Master::kMaxVoices; v++) {
                if (synth.master().voice(v).active == 0) continue;
                const int f = synth.master().filterFmSource(v);
                *voicesFm = *voicesFm == -1000 || *voicesFm == f ? f : -2;
            }
        }
    };
    auto rms = [](const std::vector<float>& v) {
        double s = 0;
        for (const float x : v) s += double(x) * x;
        return std::sqrt(s / double(v.size()));
    };
    auto peakOf = [](const std::vector<float>& v) {
        double m = 0;
        for (const float x : v) m = std::max(m, double(std::fabs(x)));
        return m;
    };

    // OFF, a source byte no version writes and an amount of 0 render exactly alike, for every
    // type, on INIT and on programs of every bank; so does FM from an oscillator whose OUTPUT
    // is off (OSC3 of the plain INIT program).
    int checked = 0;
    for (const int prog : {-1, 0, 100, 200, 300, 400, 500}) {
        for (int type = 0; type < FilterType::Count; type++) {
            Setup s;
            s.prog = prog;
            s.type = type;
            std::vector<float> L0, R0;
            render(44100.0f, s, L0, R0);
            const uint64_t off = hashLR(L0, R0);
            const std::tuple<int, int, const char*> cases[] = {
                {9, 40, "source byte 9"}, {200, -40, "source byte 200"}, {1, 0, "OSC1 at amount 0"}, {3, 0, "OSC3 at amount 0"}};
            for (const auto& [src, amt, what] : cases) {
                Setup t = s;
                t.src = src;
                t.amt = amt;
                std::vector<float> L, R;
                render(44100.0f, t, L, R);
                checked++;
                if (hashLR(L, R) != off)
                    failP("program " + std::to_string(prog) + " " + names[type] + ": " + what + " does not render like OFF");
            }
        }
    }
    for (int type = 0; type < FilterType::Count; type++) {
        Setup s;
        s.plainInit = true;
        s.type = type;
        std::vector<float> La, Ra, Lb, Rb;
        render(44100.0f, s, La, Ra);
        s.src = 3, s.amt = 63;
        render(44100.0f, s, Lb, Rb);
        checked++;
        if (hashLR(La, Ra) != hashLR(Lb, Rb)) failP(std::string("INIT ") + names[type] + ": OSC3 with its OUTPUT off modulates");
    }
    std::printf("programs: OFF, unknown source bytes, amount 0 and a disabled oscillator render alike (%d renders, %s)\n",
                checked, progBad ? "FAILED" : "ok");

    // Extremes: every type, every source, +-63, RES 31, the cutoff at the bottom, middle and
    // top at 44.1 kHz, in the middle at 96 and 192 kHz: finite and bounded. Then a chord, and
    // FM switched between sources every block while the notes play.
    int extremes = 0;
    double worst = 0;
    for (const float sr : {44100.0f, 96000.0f, 192000.0f})
        for (int type = 0; type < FilterType::Count; type++)
            for (int src = 1; src <= 3; src++)
                for (const int amt : {-63, 63})
                    for (const int freq : {0, 64, 127}) {
                        if (sr != 44100.0f && freq != 64) continue;
                        Setup s;
                        s.type = type, s.src = src, s.amt = amt, s.res = 31, s.freq = freq;
                        std::vector<float> L, R;
                        render(sr, s, L, R);
                        extremes++;
                        const double pk = std::max(peakOf(L), peakOf(R));
                        worst = std::max(worst, pk);
                        if (!finite(L) || !finite(R) || !(pk < 8.0))
                            failP(std::string(names[type]) + " OSC" + std::to_string(src) + " amount " + std::to_string(amt) +
                                  " FREQ " + std::to_string(freq) + " at " + std::to_string(int(sr)) +
                                  " Hz: peak " + std::to_string(pk));
                    }
    for (int type = 0; type < FilterType::Count; type++) {
        Setup s;
        s.type = type, s.src = 3, s.amt = 50, s.res = 20;
        s.keys = {48, 55, 60, 64, 67, 72};
        std::vector<float> L, R;
        int fmAll = 0;
        render(44100.0f, s, L, R, &fmAll);
        extremes++;
        if (!finite(L) || !finite(R)) failP(std::string(names[type]) + ": chord with FM not finite");
        if (fmAll != 2)
            failP(std::string(names[type]) + ": not every voice of the chord follows OSC3 (" + std::to_string(fmAll) + ")");
        s.srcPerBlock = {0, 3, 1, 0, 2};
        std::vector<float> Ls, Rs;
        render(44100.0f, s, Ls, Rs);
        extremes++;
        if (!finite(Ls) || !finite(Rs)) failP(std::string(names[type]) + ": switching FM mid-note not finite");
    }
    std::printf("programs: %d extreme renders finite, peak %.3f (%s)\n", extremes, worst, progBad ? "FAILED" : "ok");

    // Audible: the saw through each type with FREQ 50, FM from the silent OSC3 at +40 against
    // OFF. The difference carries at least a tenth of the level.
    for (int type = 0; type < FilterType::Count; type++) {
        Setup s;
        s.type = type;
        std::vector<float> L0, R0, L1, R1;
        render(44100.0f, s, L0, R0);
        s.src = 3, s.amt = 40;
        render(44100.0f, s, L1, R1);
        std::vector<float> d(L0.size());
        for (size_t i = 0; i < d.size(); i++) d[i] = L1[i] - L0[i];
        const double ratio = rms(d) / std::max(rms(L0), 1e-12);
        std::printf("  saw, FREQ 50, %-5s: rms OFF %.4f, FM %.4f, difference %.0f%%\n", names[type], rms(L0), rms(L1),
                    100 * ratio);
        if (!(ratio > 0.1) || rms(L1) < 1e-3) failP(std::string(names[type]) + ": FM from OSC3 does not change the sound");
    }
    std::printf("programs (%s)\n", progBad ? "FAILED" : "ok");
    return bad || progBad ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 2 && std::string(argv[1]) == "--regression") return regression(argv[2]);
    if (argc > 2 && std::string(argv[1]) == "--polyphony") return polyphony(argv[2]);
    if (argc > 1 && std::string(argv[1]) == "--direction") return direction();
    if (argc > 2 && std::string(argv[1]) == "--output-gain") return outputGain(argv[2]);
    if (argc > 1 && std::string(argv[1]) == "--filter-types") return filterTypes();
    if (argc > 1 && std::string(argv[1]) == "--filter-fm") return filterFm();
    return golden(argc > 1 ? argv[1] : "tests/golden_raw");
}
