#include "ConfigStore.h"

#include <Preferences.h>

namespace sb20proxy {

static const char* kNs = "proxycfg";
static const char* kKey = "cfg";

RuntimeConfig ConfigStore::load() {
    Preferences p;
    if (!p.begin(kNs, /*readOnly=*/true)) return RuntimeConfig::defaults();
    String line = p.getString(kKey, "");
    p.end();
    if (line.length() == 0) return RuntimeConfig::defaults();
    return RuntimeConfig::fromLine(std::string(line.c_str()));
}

void ConfigStore::save(const RuntimeConfig& c) {
    Preferences p;
    if (!p.begin(kNs, /*readOnly=*/false)) return;
    p.putString(kKey, c.toLine().c_str());
    p.end();
}

void ConfigStore::clear() {
    Preferences p;
    if (p.begin(kNs, /*readOnly=*/false)) {
        p.clear();
        p.end();
    }
}

}  // namespace sb20proxy
