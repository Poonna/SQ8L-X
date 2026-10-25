// The parameters the plugin publishes to the host (src/gui/logic/HostParams.h): lists them
// and checks what the plugin formats rely on. `sq8l_param_check` prints the table and exits
// with 1 on a problem; `sq8l_param_check -q` only checks.
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "EditBuffer.h"
#include "HostParams.h"
#include "Program.h"

using sq8l::gui::HostParam;

int main(int argc, char** argv) {
    const bool quiet = argc > 1 && std::strcmp(argv[1], "-q") == 0;
    const auto& params = sq8l::gui::hostParams();
    int errors = 0;
    const auto fail = [&](const HostParam& p, const char* what) {
        std::fprintf(stderr, "%s: %s\n", p.name.c_str(), what);
        errors++;
    };
    std::set<std::string> names, symbols;
    std::set<int> fields;
    sq8l::EditBuffer eb(nullptr);
    for (size_t i = 0; i < params.size(); i++) {
        const HostParam& p = params[i];
        if (!names.insert(p.name).second) fail(p, "name used twice");
        if (p.name.size() > 15) fail(p, "name longer than a VST2 host shows");
        if (p.symbol.empty() || !symbols.insert(p.symbol).second) fail(p, "symbol empty or used twice");
        if (!fields.insert(p.editIndex).second) fail(p, "edit buffer parameter published twice");
        if (p.min >= p.max) fail(p, "empty range");
        if (p.def < p.min || p.def > p.max) fail(p, "default out of range");
        if (!p.valueTexts.empty() && int(p.valueTexts.size()) != p.max - p.min + 1) fail(p, "value texts do not cover the range");
        if (p.max - p.min + 1 > 255 && !p.valueTexts.empty()) fail(p, "too many value texts for an enumeration");
        // The INIT program reads as the default, and every value of the range goes in and
        // comes back out unchanged.
        eb.initProgram();
        if (sq8l::gui::hostParamValue(eb, p) != p.def) fail(p, "INIT program does not read as the default");
        for (int v = p.min; v <= p.max; v++) {
            sq8l::gui::setHostParamValue(eb, p, v);
            if (sq8l::gui::hostParamValue(eb, p) != v) {
                fail(p, "value does not round-trip");
                break;
            }
        }
        if (!quiet) {
            std::printf("%3zu  %-20s %-20s ep %3d  %4d..%4d  def %4d", i, p.name.c_str(), p.symbol.c_str(), p.editIndex,
                        p.min, p.max, p.def);
            if (!p.valueTexts.empty())
                std::printf("  [%s .. %s]", p.valueTexts.front().c_str(), p.valueTexts.back().c_str());
            std::printf("\n");
        }
    }
    // A parameter written through the table touches its own field only.
    sq8l::EditBuffer ref(nullptr);
    eb.initProgram();
    ref.initProgram();
    for (const HostParam& p : params) {
        const int v = sq8l::gui::hostParamValue(eb, p) == p.max ? p.min : p.max;
        sq8l::gui::setHostParamValue(eb, p, v);
        for (const HostParam& q : params)
            if (&q != &p && sq8l::gui::hostParamValue(eb, q) != sq8l::gui::hostParamValue(ref, q))
                fail(p, ("also changes " + q.name).c_str());
        eb.current() = ref.current();
    }
    std::printf("%zu parameters, %d problem(s)\n", params.size(), errors);
    return errors ? 1 : 0;
}
