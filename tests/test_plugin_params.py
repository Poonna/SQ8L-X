"""The parameters the BUILT plugin publishes to the host (src/gui/logic/HostParams.h), through
the VST2 ABI: no emulator needed.

    python tests/test_plugin_params.py [path to the VST2 binary]

Default: build/clang-cl/bin/SQ8L-X.dll (Windows), build/unix/bin/SQ8L-X.so (Linux),
build/unix/bin/SQ8L-X.vst/Contents/MacOS/SQ8L-X (macOS).

Checks: the count, names and value texts; that a value set by the host lands in the program
(the chunk), changes the sound and reads back, whatever the host believed the value was;
that repeating a value leaves the program alone; that program changes and chunks show in
the values.
"""
import ctypes as C
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if sys.platform == "win32":
    DEFAULT = os.path.join(ROOT, "build", "clang-cl", "bin", "SQ8L-X.dll")
elif sys.platform == "darwin":
    DEFAULT = os.path.join(ROOT, "build", "unix", "bin", "SQ8L-X.vst", "Contents", "MacOS", "SQ8L-X")
else:
    DEFAULT = os.path.join(ROOT, "build", "unix", "bin", "SQ8L-X.so")

HOSTCB = C.CFUNCTYPE(C.c_ssize_t, C.c_void_p, C.c_int32, C.c_int32, C.c_ssize_t, C.c_void_p, C.c_float)
DISPATCH = C.CFUNCTYPE(C.c_ssize_t, C.c_void_p, C.c_int32, C.c_int32, C.c_ssize_t, C.c_void_p, C.c_float)
SETPARAM = C.CFUNCTYPE(None, C.c_void_p, C.c_int32, C.c_float)
GETPARAM = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_int32)
PROCESS = C.CFUNCTYPE(None, C.c_void_p, C.POINTER(C.POINTER(C.c_float)), C.POINTER(C.POINTER(C.c_float)), C.c_int32)


class AEffect(C.Structure):
    _fields_ = [("magic", C.c_int32), ("dispatcher", DISPATCH), ("process", C.c_void_p),
                ("setParameter", SETPARAM), ("getParameter", GETPARAM),
                ("numPrograms", C.c_int32), ("numParams", C.c_int32), ("numInputs", C.c_int32),
                ("numOutputs", C.c_int32), ("flags", C.c_int32), ("resvd1", C.c_ssize_t), ("resvd2", C.c_ssize_t),
                ("initialDelay", C.c_int32), ("realQualities", C.c_int32), ("offQualities", C.c_int32),
                ("ioRatio", C.c_float), ("object", C.c_void_p), ("user", C.c_void_p),
                ("uniqueID", C.c_int32), ("version", C.c_int32), ("processReplacing", PROCESS),
                ("processDoubleReplacing", C.c_void_p), ("future", C.c_char * 56)]


class MidiEv(C.Structure):
    _fields_ = [("type", C.c_int32), ("byteSize", C.c_int32), ("deltaFrames", C.c_int32), ("flags", C.c_int32),
                ("noteLength", C.c_int32), ("noteOffset", C.c_int32), ("midiData", C.c_uint8 * 4),
                ("detune", C.c_int8), ("noteOffVelocity", C.c_uint8), ("r1", C.c_int8), ("r2", C.c_int8)]


class Events(C.Structure):
    _fields_ = [("numEvents", C.c_int32), ("reserved", C.c_ssize_t), ("events", C.c_void_p * 2)]


automated = []


def host_cb(effect, opcode, index, value, ptr, opt):
    if opcode == 0:      # audioMasterAutomate
        automated.append((index, opt))
    if opcode == 1:      # version
        return 2400
    if opcode == 16:     # sample rate
        return 44100
    if opcode == 17:     # block size
        return 512
    return 0


_cb = HOSTCB(host_cb)

effOpen, effSetProgram, effGetProgram = 0, 2, 3
effGetParamDisplay, effGetParamName = 7, 8
effSetSampleRate, effSetBlockSize, effMainsChanged = 10, 11, 12
effGetChunk, effSetChunk, effProcessEvents, effCanBeAutomated = 23, 24, 25, 26
effStartProcess = 71


class Plugin:
    def __init__(self, path):
        self.lib = C.CDLL(path)
        entry = getattr(self.lib, "VSTPluginMain")
        entry.restype = C.POINTER(AEffect)
        entry.argtypes = [HOSTCB]
        self.eff = entry(_cb)
        self.e = self.eff.contents
        self.p = C.cast(self.eff, C.c_void_p)
        self.d(effOpen)
        self.d(effSetSampleRate, opt=44100.0)
        self.d(effSetBlockSize, value=512)
        self.d(effMainsChanged, value=1)
        self.d(effStartProcess)

    def d(self, op, index=0, value=0, ptr=None, opt=0.0):
        return self.e.dispatcher(self.p, op, index, value, ptr, opt)

    def text(self, op, index):
        buf = C.create_string_buffer(256)
        self.d(op, index, 0, C.cast(buf, C.c_void_p))
        return buf.value.decode("latin-1")

    def name(self, i):
        return self.text(effGetParamName, i)

    def display(self, i):
        return self.text(effGetParamDisplay, i)

    def get(self, i):
        return self.e.getParameter(self.p, i)

    def set(self, i, normalized):
        self.e.setParameter(self.p, i, normalized)

    def chunk(self):
        ptr = C.c_void_p()
        n = self.d(effGetChunk, 0, 0, C.cast(C.byref(ptr), C.c_void_p))
        return C.string_at(ptr, n)

    def set_chunk(self, data):
        buf = C.create_string_buffer(data, len(data))
        self.d(effSetChunk, 0, len(data), C.cast(buf, C.c_void_p))

    def render_note(self, frames=44100 // 2, note=48):
        """RMS of a held note (the voices stopped first by the program's own sound)."""
        ev = MidiEv(1, C.sizeof(MidiEv), 0, 0, 0, 0, (C.c_uint8 * 4)(0x90, note, 100, 0))
        evs = Events(1, 0)
        evs.events[0] = C.cast(C.pointer(ev), C.c_void_p)
        self.d(effProcessEvents, 0, 0, C.cast(C.pointer(evs), C.c_void_p))
        L, R = (C.c_float * 512)(), (C.c_float * 512)()
        outs = (C.POINTER(C.c_float) * 2)(C.cast(L, C.POINTER(C.c_float)), C.cast(R, C.POINTER(C.c_float)))
        acc, n = 0.0, 0
        for _ in range(frames // 512):
            self.e.processReplacing(self.p, None, outs, 512)
            acc += sum(x * x for x in L)
            n += 512
        off = MidiEv(1, C.sizeof(MidiEv), 0, 0, 0, 0, (C.c_uint8 * 4)(0x80, note, 0, 0))
        evs.events[0] = C.cast(C.pointer(off), C.c_void_p)
        self.d(effProcessEvents, 0, 0, C.cast(C.pointer(evs), C.c_void_p))
        for _ in range(88200 // 512):  # let it ring out
            self.e.processReplacing(self.p, None, outs, 512)
        return math.sqrt(acc / n)


failures = 0


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    pl = Plugin(path)
    n = pl.e.numParams
    check(n == 228, f"{n} parameters published")
    names = {pl.name(i): i for i in range(n)}
    check(len(names) == n, "names are unique")
    check(all(pl.d(effCanBeAutomated, i) == 1 for i in range(n)), "every parameter can be automated")
    for nm in ("OSC1 WAVE", "FILT FREQ", "DCA4 PAN", "ENV1 T1", "EMU VOICES"):
        found = [k for k in names if k.upper() == nm]
        check(bool(found), f"'{nm}' is published")
    idx = {k.upper(): v for k, v in names.items()}
    wave, freq, pan = idx["OSC1 WAVE"], idx["FILT FREQ"], idx["DCA4 PAN"]
    voices = idx["EMU VOICES"]

    # Values as the display names them: OSC1 WAVE 0..74, 0 = SAW.
    pl.set(wave, 0.0)
    check(pl.display(wave) == "SAW", f"OSC1 WAVE at 0 shows '{pl.display(wave)}'")
    pl.set(wave, 1.0)
    check(pl.display(wave) != "SAW" and abs(pl.get(wave) - 1.0) < 1e-6, f"OSC1 WAVE at 74 shows '{pl.display(wave)}'")

    # A value set by the host is the program's: in the chunk, read back, and heard.
    pl.d(effSetProgram, 0, 0)
    pl.set(wave, 0.0)                               # a steady saw
    pl.set(freq, 1.0)
    before = pl.chunk()
    bright = pl.render_note()
    pl.set(freq, 0.0)
    dark = pl.render_note()
    check(abs(pl.get(freq)) < 1e-6, "FILT FREQ reads back 0")
    check(pl.chunk() != before, "the chunk carries the edit")
    # (VOYAGER opens its filter with ENV3 and the keyboard too: it never closes completely)
    check(bright > 0.0 and dark < bright * 0.8, f"closing the filter darkens the sound (rms {bright:.4f} -> {dark:.4f})")
    final = idx["DCA4 ENV4.AMT"]
    level = pl.get(final)
    pl.set(final, 0.0)
    silent = pl.render_note()
    check(silent < dark * 0.1, f"DCA4 ENV4.AMT at 0 silences it (rms {silent:.4f})")
    pl.set(final, level)
    pl.set(freq, 1.0)
    check(abs(pl.get(freq) - 1.0) < 1e-6, "FILT FREQ reads back 127")

    # The host sending the value it has changes nothing, not even the modified flag.
    c1 = pl.chunk()
    pl.set(freq, 1.0)
    check(pl.chunk() == c1, "repeating a value leaves the program alone")

    # Program changes show in the values.
    vals = {}
    for prog in (0, 1, 2, 3):
        pl.d(effSetProgram, 0, prog)
        vals[prog] = [round(pl.get(i), 5) for i in range(n)]
    check(len({tuple(v) for v in vals.values()}) == 4, "programs 0-3 read as four different sets of values")

    # A chunk replaces the values, and the host can set them again from there.
    pl.d(effSetProgram, 0, 0)
    pl.set(pan, 1.0)                                # +63
    saved = pl.chunk()
    pl.set(pan, 0.0)                                # -63
    pl.set_chunk(saved)
    check(abs(pl.get(pan) - 1.0) < 1e-6, "a chunk brings its values back")
    pl.set(pan, 0.0)
    check(abs(pl.get(pan)) < 1e-6, "the host can set a value again after a chunk")

    # VOICES: a program of the original stores 0 and plays 8; the host sees 8.
    pl.d(effSetProgram, 0, 0)
    check(pl.display(voices) == "8" or abs(pl.get(voices) * 63 + 1 - 8) < 0.01,
          f"VOICES of a factory program shows 8 ('{pl.display(voices)}')")

    print(f"{failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
