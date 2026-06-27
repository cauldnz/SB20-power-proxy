#pragma once
#include <string>
#include <vector>

namespace sb20proxy {

// A few built-in structured workouts (canonical workout JSON) so the Workout screen has something to
// run before the desk ZWO/FIT importers (§14 phase 1) land. %FTP-based with a default ftp_w the rider
// can edit; the WorkoutEngine resolves them to watts. Pure constants — host-tested.
struct WorkoutPreset {
    const char* key;
    const char* label;
    const char* json;
};

inline const std::vector<WorkoutPreset>& workoutPresets() {
    static const std::vector<WorkoutPreset> presets = {
        {"4x8", "4×8 Threshold",
         R"({"name":"4x8 Threshold","ftp_w":250,"segments":[)"
         R"({"t":600,"label":"Warm-up","pct_ftp":0.55},)"
         R"({"t":480,"label":"Interval 1","pct_ftp":0.99,"cadence_rpm":90},)"
         R"({"t":120,"label":"Recovery","pct_ftp":0.50},)"
         R"({"t":480,"label":"Interval 2","pct_ftp":0.99,"cadence_rpm":90},)"
         R"({"t":120,"label":"Recovery","pct_ftp":0.50},)"
         R"({"t":480,"label":"Interval 3","pct_ftp":0.99,"cadence_rpm":90},)"
         R"({"t":120,"label":"Recovery","pct_ftp":0.50},)"
         R"({"t":480,"label":"Interval 4","pct_ftp":0.99,"cadence_rpm":90},)"
         R"({"t":300,"label":"Cool-down","pct_ftp":0.45}]})"},
        {"ss3x12", "Sweet Spot 3×12",
         R"({"name":"Sweet Spot 3x12","ftp_w":250,"segments":[)"
         R"({"t":600,"label":"Warm-up","pct_ftp":0.55},)"
         R"({"t":720,"label":"Sweet Spot 1","pct_ftp":0.90},)"
         R"({"t":300,"label":"Recovery","pct_ftp":0.50},)"
         R"({"t":720,"label":"Sweet Spot 2","pct_ftp":0.90},)"
         R"({"t":300,"label":"Recovery","pct_ftp":0.50},)"
         R"({"t":720,"label":"Sweet Spot 3","pct_ftp":0.90},)"
         R"({"t":300,"label":"Cool-down","pct_ftp":0.45}]})"},
        {"vo25x3", "VO2 5×3",
         R"({"name":"VO2 5x3","ftp_w":250,"segments":[)"
         R"({"t":600,"label":"Warm-up","pct_ftp":0.55},)"
         R"({"t":180,"label":"VO2 1","pct_ftp":1.12,"cadence_rpm":95},)"
         R"({"t":180,"label":"Recovery","pct_ftp":0.45},)"
         R"({"t":180,"label":"VO2 2","pct_ftp":1.12,"cadence_rpm":95},)"
         R"({"t":180,"label":"Recovery","pct_ftp":0.45},)"
         R"({"t":180,"label":"VO2 3","pct_ftp":1.12,"cadence_rpm":95},)"
         R"({"t":180,"label":"Recovery","pct_ftp":0.45},)"
         R"({"t":180,"label":"VO2 4","pct_ftp":1.12,"cadence_rpm":95},)"
         R"({"t":180,"label":"Recovery","pct_ftp":0.45},)"
         R"({"t":180,"label":"VO2 5","pct_ftp":1.12,"cadence_rpm":95},)"
         R"({"t":300,"label":"Cool-down","pct_ftp":0.45}]})"},
        {"endur45", "Endurance 45",
         R"({"name":"Endurance 45","ftp_w":250,"segments":[)"
         R"({"t":300,"label":"Warm-up","pct_ftp":0.55},)"
         R"({"t":2100,"label":"Endurance","pct_ftp":0.68},)"
         R"({"t":300,"label":"Cool-down","pct_ftp":0.45}]})"},
    };
    return presets;
}

// The canonical workout JSON for a preset key, or "" if unknown. Pure.
inline std::string presetJson(const std::string& key) {
    for (const auto& p : workoutPresets())
        if (key == p.key) return p.json;
    return "";
}

}  // namespace sb20proxy
