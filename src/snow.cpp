// Falling flakes: spawning, wind, gusts, landing
#include "snow.h"

static constexpr float kPi = 3.14159265f;

static float SpeedBase(float scale) { return (25.0f + Cfg().fallSpeed * 1.5f) * scale; }

static constexpr float kMaxFlakes = 6000;  // cap on flakes in the air at once (performance)

// Flakes per monitor follow its logical area rather than pixels,
// so a 4K screen at 150% is not denser than plain Full HD
static float MonitorTarget(const MonitorInfo& m) {
    return Cfg().intensity / 100.0f * 1500.0f * m.LogicalArea() / (1920.0f * 1080.0f);
}

void Snow::SpawnFlake(const World& w, bool anywhere) {
    const Settings& c = Cfg();
    // Pick a monitor weighted by area and spawn above it,
    // so monitors of different heights all get snow
    RectI mr = {0, 0, w.virt.w(), w.virt.h()};
    float sc = scale_;
    if (!w.monitors.empty()) {
        float total = 0;
        for (const auto& m : w.monitors) total += m.LogicalArea();
        float pick = Rand01() * total;
        for (const auto& m : w.monitors) {
            mr = m.rc;
            sc = m.scale;
            pick -= m.LogicalArea();
            if (pick <= 0) break;
        }
    }
    Flake f{};
    f.s = sc;
    f.z = 0.35f + 0.65f * std::pow(Rand01(), 1.3f);
    float sizeBase = (1.6f + c.flakeSize / 100.0f * 4.4f) * sc;
    f.size = sizeBase * (0.45f + 0.75f * f.z) * RandRange(0.8f, 1.2f);
    f.mass = f.size * f.size * 0.9f;
    f.kind = FLAKE_DOT;
    f.alpha = 0.35f + 0.65f * f.z;
    if (Rand01() * 100 < c.crystalShare) {
        // Crystal at any depth: farther ones are smaller and fainter but still readable
        f.kind = FLAKE_CRYSTAL;
        f.variant = (uint8_t)std::min(kCrystalVariants - 1, (int)(Rand01() * kCrystalVariants));
        f.size = std::max(6.0f * sc, f.size * RandRange(2.2f, 3.4f));
        f.alpha = 0.5f + 0.45f * f.z;
    }
    // Snow falls at an angle: widen the spawn band so the screen fills evenly
    float tanA = std::tan(angleDeg_ * kPi / 180.0f);
    float drift = mr.h() * tanA;
    f.x = RandRange(std::min((float)mr.l, mr.l - drift) - 40, std::max((float)mr.r, mr.r - drift) + 40);
    f.y = anywhere ? RandRange(mr.t - 20.0f, mr.t + mr.h() * 0.9f) : mr.t - f.size - Rand01() * 30;
    f.vy = SpeedBase(sc) * (0.45f + 0.55f * f.z);
    f.vx = f.vy * tanA;
    f.phase = Rand01() * 2 * kPi;
    f.freq = RandRange(0.6f, 1.8f);
    f.rot = Rand01() * 2 * kPi;
    f.vrot = RandRange(-1.2f, 1.2f);
    flakes.push_back(f);
}

void Snow::Spawn(float dt, const World& w) {
    // How many flakes to keep in the air and how often to add them, per monitor
    float target = 0, rate = 0;
    for (const auto& m : w.monitors) {
        float t = MonitorTarget(m);
        target += t;
        rate += t * SpeedBase(m.scale) * 0.72f / (m.rc.h() + 40);
    }
    if (target > kMaxFlakes) {
        rate *= kMaxFlakes / target;
        target = kMaxFlakes;
    }
    if (target < 1) return;
    if (!prefilled_) {
        for (int i = 0; i < (int)(target * 0.35f); ++i) SpawnFlake(w, true);
        prefilled_ = true;
    }
    int live = 0;
    for (const auto& f : flakes) live += f.kind != FLAKE_CHUNK;
    spawnAcc_ = std::min(spawnAcc_ + rate * dt, 200.0f);
    while (spawnAcc_ >= 1 && live < target * 1.2f) {
        SpawnFlake(w, false);
        spawnAcc_ -= 1;
        ++live;
    }
}

void Snow::Update(float dt, const World& w, POINT cur, bool cursorValid) {
    const Settings& c = Cfg();
    time_ += dt;
    accumK_ = c.accumulation <= 0 ? 0.0f : std::pow(c.accumulation / 50.0f, 1.6f);

    // Smoothed cursor velocity
    if (cursorValid && havePrevCursor_ && dt > 0) {
        float k = std::min(1.0f, dt * 12);
        curVx_ += ((cur.x - prevCursor_.x) / dt - curVx_) * k;
        curVy_ += ((cur.y - prevCursor_.y) / dt - curVy_) * k;
    } else {
        curVx_ = curVy_ = 0;
    }
    prevCursor_ = cur;
    havePrevCursor_ = cursorValid;

    // Wind follows the cursor: near the left edge of a monitor snow drifts left, near the right edge right
    float cursorTarget = 0;
    if (cursorValid && c.cursorWind > 0) {
        for (const auto& m : w.monitors) {
            if (cur.x < m.rc.l || cur.x >= m.rc.r || cur.y < m.rc.t || cur.y >= m.rc.b) continue;
            float nx = (cur.x - (m.rc.l + m.rc.r) * 0.5f) / std::max(1.0f, m.rc.w() * 0.5f);
            cursorTarget = Clampf(nx, -1, 1) * c.cursorWind / 100.0f * 40.0f;
            break;
        }
    }
    cursorAngle_ += (cursorTarget - cursorAngle_) * std::min(1.0f, dt * 1.2f);  // eased, no jumps
    angleDeg_ = Clampf(c.angle + cursorAngle_, -75, 75);

    Spawn(dt, w);
    if (cursorValid && c.cursorKnock) Knock(dt, cur);

    const float W = (float)w.virt.w(), H = (float)w.virt.h();
    const float speed = SpeedBase(1.0f);  // multiplied by each flake's monitor scale
    const float tanA = std::tan(angleDeg_ * kPi / 180.0f);
    const float gust = c.gusts / 100.0f * speed * 0.9f *
                       (std::sin(time_ * 0.21f) * 0.6f + std::sin(time_ * 0.57f + 1.7f) * 0.3f +
                        std::sin(time_ * 1.31f + 0.4f) * 0.1f);
    const float wobble = 8 + 22 * c.gusts / 100.0f;
    const float relaxK = std::min(1.0f, dt * 2.5f);
    const bool behind = c.mode == MODE_BEHIND;
    const float pushR = c.cursorRadius * ScaleAt((float)cur.x, (float)cur.y) * 2.2f;
    const float curSpeed2 = curVx_ * curVx_ + curVy_ * curVy_;
    const bool push = cursorValid && curSpeed2 > 900.0f;

    for (size_t i = 0; i < flakes.size();) {
        Flake& f = flakes[i];
        if (f.kind == FLAKE_SLIDE) {
            if (!MoveSlide(f, dt)) {  // fell off the corner: keep falling as a clump
                f.kind = FLAKE_CHUNK;
                f.vy = std::fabs(f.vx) * 0.25f;
                f.vx *= 0.8f;
            }
            ++i;
            continue;
        }
        float prevY = f.y;
        if (f.kind == FLAKE_CHUNK) {
            f.vy = std::min(f.vy + 900.0f * f.s * dt, 700.0f * f.s);
            f.vx *= std::max(0.0f, 1 - 1.5f * dt);
        } else {
            float tvy = speed * f.s * (0.45f + 0.55f * f.z) * (f.kind == FLAKE_CRYSTAL ? 0.8f : 1.0f);
            float tvx = tvy * tanA + (gust * (0.4f + 0.6f * f.z) + std::sin(time_ * f.freq + f.phase) * wobble * f.z) * f.s;
            f.vx += (tvx - f.vx) * relaxK;
            f.vy += (tvy - f.vy) * relaxK;
            f.rot += f.vrot * dt;
            if (push) {  // the moving cursor blows flakes aside
                float dx = f.x - cur.x, dy = f.y - cur.y;
                float d2 = dx * dx + dy * dy;
                if (d2 < pushR * pushR) {
                    float infl = 1 - std::sqrt(d2) / pushR;
                    f.vx += curVx_ * infl * 0.12f;
                    f.vy += curVy_ * infl * 0.12f;
                }
            }
        }
        f.x += f.vx * dt;
        f.y += f.vy * dt;

        bool gone = f.y > H + 30 || f.x < -500 || f.x > W + 500;
        if (!gone && (f.z > 0.62f || f.kind == FLAKE_CHUNK) && f.vy > 0) {
            for (auto& s : surfaces) {
                if (f.x < s.left || f.x >= s.left + s.width) continue;
                int cell = std::min(s.cells - 1, (int)((f.x - s.left) / kCell));
                float sy = s.top - s.h[cell];
                if (prevY > sy || f.y < sy) continue;
                // "Over windows" mode: the flake passes in front of a window that hides the drift
                if (!behind && s.h[cell] + 0.5f < s.hideBelow[cell]) continue;
                // Windows sit at different depths: an edge catches only some flakes, otherwise windows lower
                // on screen stay bare under the top edges of other windows
                if (!s.ground && f.kind != FLAKE_CHUNK && Rand01() > 0.6f) continue;
                Land(f, s, cell);
                gone = true;
                break;
            }
        }
        if (gone) {
            flakes[i] = flakes.back();
            flakes.pop_back();
        } else {
            ++i;
        }
    }
    for (auto& s : surfaces) Relax(s, dt);
}

bool Snow::VisibleMotion() const {
    const bool falling = Cfg().showFalling;
    for (const auto& f : flakes)
        if (falling || f.kind == FLAKE_CHUNK || f.kind == FLAKE_SLIDE) return true;
    return false;
}

bool Snow::Idle() const {
    if (!flakes.empty()) return false;
    for (const auto& s : surfaces)
        for (float v : s.h)
            if (v > 0.2f) return false;
    return true;
}
