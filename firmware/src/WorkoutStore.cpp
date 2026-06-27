#include "WorkoutStore.h"

#include <Preferences.h>

namespace sb20proxy {

static const char* kNs = "workout";
static const char* kKey = "json";

std::string WorkoutStore::load() {
    Preferences p;
    if (!p.begin(kNs, /*readOnly=*/true)) return "";
    String j = p.getString(kKey, "");
    p.end();
    return std::string(j.c_str());
}

void WorkoutStore::save(const std::string& json) {
    Preferences p;
    if (!p.begin(kNs, /*readOnly=*/false)) return;
    p.putString(kKey, json.c_str());
    p.end();
}

void WorkoutStore::clear() {
    Preferences p;
    if (p.begin(kNs, /*readOnly=*/false)) {
        p.clear();
        p.end();
    }
}

}  // namespace sb20proxy
