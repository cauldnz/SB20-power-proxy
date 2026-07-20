#pragma once
// WattyBird — a pure, host-tested "Flappy Bird" core flown by the rider's POWER. 🐤
//
// The bird's lift is proportional to watts: there is a HOVER wattage where lift exactly cancels
// gravity and the bird holds altitude; pedal above it to climb, ease below it to sink. So threading
// the pipes is really a power-modulation drill in disguise (find hover, surge to climb, back off to
// drop). Cadence rides along for the HUD. No rendering, no Arduino, and no RNG surprises — a seeded
// LCG places the gaps, so a fixed seed replays identically and the whole thing is unit-testable.
//
// This is the Easter-egg's brain: the CYD Ride-screen takeover drives step(dtMs, watts, cadence)
// each frame and hands the state to WattyBirdRender. Mirrors the project's pure-core discipline
// (like ProxyCore/Cps): logic here, pixels in the renderer, hardware only at the very edge.
#include <cstdint>
#include <vector>

namespace sb20proxy {

// Tunables — all in pixels + seconds. Defaults target the CYD's 240x320 portrait play area and a
// hover around ~145 W (gravity/liftPerWatt), so easy spinning sinks, tempo holds, and surges climb.
struct WattyBirdConfig {
    int   worldW = 240;
    int   worldH = 320;
    int   groundH = 24;         // solid ground strip along the bottom
    int   ceilingY = 0;
    int   birdX = 64;           // the bird's fixed horizontal lane
    int   birdR = 8;
    float gravity = 440.0f;     // downward accel (px/s^2) — gentle enough to control
    float liftPerWatt = 3.4f;   // upward accel per watt  ->  hover watts = gravity / liftPerWatt (~129 W)
    float maxFallV = 230.0f;    // terminal velocities keep the bird controllable
    float maxRiseV = 230.0f;
    int   startWatts = 40;      // Ready -> Playing once the rider pedals above this
    int   pipeW = 34;
    int   gapH = 108;           // vertical gap height (generous = controllable)
    int   pipeSpacing = 168;    // horizontal px between successive pipes (reaction room)
    float scrollV = 66.0f;      // px/s the world scrolls left
    int   gapMargin = 28;       // keep gaps off the ceiling/ground
    int   startRunwayPx = 60;   // extra gap before the first pipe after starting
};

struct WbPipe {
    float x;            // left edge, world px (scrolls down toward 0)
    int   gapCenterY;   // gap center, world px
    bool  passed;       // scored already?
};

enum class WbMode : uint8_t { Ready, Playing, Dead };

class WattyBird {
public:
    explicit WattyBird(const WattyBirdConfig& cfg = WattyBirdConfig()) : cfg_(cfg) { reset(0x51B2Du); }

    // Full reset back to the Ready (attract) state. `seed` seeds the deterministic pipe RNG.
    void reset(uint32_t seed) {
        mode_ = WbMode::Ready;
        birdY_ = cfg_.worldH * 0.42f;
        vy_ = 0.0f;
        score_ = 0;
        watts_ = lastWatts_ = 0;
        cadence_ = -1;
        elapsedMs_ = 0;
        seed_ = seed ? seed : 0x51B2Du;
        rng_ = seed_;
        pipes_.clear();
    }

    // Begin a run (Ready -> Playing). Re-seeds so a retry after Dead gets a fresh course; the best
    // score survives (reset() leaves best_ untouched). First pipe sits a runway beyond the right
    // edge so the player gets a beat to settle before the first gap.
    void start(uint32_t seed) {
        reset(seed);
        mode_ = WbMode::Playing;
        pipes_.push_back(WbPipe{(float)cfg_.worldW + cfg_.startRunwayPx, nextGapCenter(), false});
    }

    // Advance one tick. watts drives lift; cadence is HUD-only. Safe to call in any mode.
    void step(uint32_t dtMs, int watts, int cadence = -1) {
        lastWatts_ = watts_;
        watts_ = watts < 0 ? 0 : watts;
        cadence_ = cadence;
        elapsedMs_ += dtMs;
        const float dt = (float)dtMs / 1000.0f;

        if (mode_ == WbMode::Ready) {
            if (watts_ > cfg_.startWatts) start(seed_ + elapsedMs_ + 1u);  // pedal to fly
            return;
        }
        if (mode_ == WbMode::Dead) return;
        if (dt <= 0.0f) return;

        // --- flight: net accel = gravity minus power-lift; a hover wattage cancels gravity ---
        const float netA = cfg_.gravity - cfg_.liftPerWatt * (float)watts_;
        vy_ += netA * dt;
        if (vy_ > cfg_.maxFallV) vy_ = cfg_.maxFallV;
        if (vy_ < -cfg_.maxRiseV) vy_ = -cfg_.maxRiseV;
        birdY_ += vy_ * dt;

        // --- world scroll, spawn ahead, despawn behind ---
        for (auto& p : pipes_) p.x -= cfg_.scrollV * dt;
        while (pipes_.empty() || pipes_.back().x < (float)cfg_.worldW) {
            const float nx = pipes_.empty() ? (float)cfg_.worldW + cfg_.startRunwayPx
                                            : pipes_.back().x + (float)cfg_.pipeSpacing;
            pipes_.push_back(WbPipe{nx, nextGapCenter(), false});
        }
        while (!pipes_.empty() && pipes_.front().x + cfg_.pipeW < 0.0f) pipes_.erase(pipes_.begin());

        // --- scoring: a pipe fully behind the bird's lane counts ---
        for (auto& p : pipes_) {
            if (!p.passed && p.x + cfg_.pipeW < (float)(cfg_.birdX - cfg_.birdR)) {
                p.passed = true;
                ++score_;
                if (score_ > best_) best_ = score_;
            }
        }

        // --- collision: ceiling, ground, or a pipe body ---
        if (collides()) mode_ = WbMode::Dead;
    }

    // Hover wattage — where lift cancels gravity (the altitude-hold power). Handy for the HUD marker.
    int hoverWatts() const {
        return cfg_.liftPerWatt > 0.0f ? (int)(cfg_.gravity / cfg_.liftPerWatt + 0.5f) : 0;
    }
    bool thrusting() const { return (float)watts_ > cfg_.gravity / cfg_.liftPerWatt; }

    // --- accessors (the renderer reads these) ---
    WbMode mode() const { return mode_; }
    float birdY() const { return birdY_; }
    float vy() const { return vy_; }
    int score() const { return score_; }
    int best() const { return best_; }
    void setBest(int b) { best_ = b > 0 ? b : 0; }
    int watts() const { return watts_; }
    int cadence() const { return cadence_; }
    uint32_t elapsedMs() const { return elapsedMs_; }
    const std::vector<WbPipe>& pipes() const { return pipes_; }
    const WattyBirdConfig& cfg() const { return cfg_; }
    int groundY() const { return cfg_.worldH - cfg_.groundH; }

private:
    int nextGapCenter() {
        int lo = cfg_.gapMargin + cfg_.gapH / 2;
        int hi = (cfg_.worldH - cfg_.groundH) - cfg_.gapMargin - cfg_.gapH / 2;
        if (hi < lo) hi = lo;
        rng_ = rng_ * 1103515245u + 12345u;                 // classic LCG, deterministic per seed
        return lo + (int)((rng_ >> 16) % (uint32_t)(hi - lo + 1));
    }

    bool collides() const {
        const float top = birdY_ - (float)cfg_.birdR;
        const float bot = birdY_ + (float)cfg_.birdR;
        if (top < (float)cfg_.ceilingY) return true;
        if (bot > (float)(cfg_.worldH - cfg_.groundH)) return true;
        const float bl = (float)(cfg_.birdX - cfg_.birdR);
        const float br = (float)(cfg_.birdX + cfg_.birdR);
        for (const auto& p : pipes_) {
            if (br < p.x || bl > p.x + (float)cfg_.pipeW) continue;  // no horizontal overlap
            const float gapTop = (float)(p.gapCenterY - cfg_.gapH / 2);
            const float gapBot = (float)(p.gapCenterY + cfg_.gapH / 2);
            if (top < gapTop || bot > gapBot) return true;          // outside the gap = hit a pipe
        }
        return false;
    }

    WattyBirdConfig cfg_;
    WbMode mode_ = WbMode::Ready;
    float birdY_ = 0.0f, vy_ = 0.0f;
    int score_ = 0, best_ = 0;
    int watts_ = 0, lastWatts_ = 0, cadence_ = -1;
    uint32_t elapsedMs_ = 0;
    uint32_t seed_ = 0x51B2Du, rng_ = 0x51B2Du;
    std::vector<WbPipe> pipes_;
};

}  // namespace sb20proxy
