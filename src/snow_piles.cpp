// Drifts: binding to windows, occlusion, sliding off, cursor interaction
#include "snow.h"

static constexpr uintptr_t kGroundKeyBase = ~(uintptr_t)0 - 64;

// Smooth value noise for bumps and hills
static void SmoothNoise(std::vector<float>& out, int n, int period, float lo, float hi) {
    period = std::max(1, period);
    std::vector<float> ctrl(n / period + 2);
    for (auto& v : ctrl) v = RandRange(lo, hi);
    out.resize(n);
    for (int i = 0; i < n; ++i) {
        float t = (float)i / period;
        int k = (int)t;
        float f = t - k;
        f = f * f * (3 - 2 * f);
        out[i] = ctrl[k] * (1 - f) + ctrl[k + 1] * f;
    }
}

void Snow::InitSurface(Surface& s, uintptr_t key, bool ground, int left, int top, int width) {
    s.key = key;
    s.ground = ground;
    s.left = left;
    s.top = top;
    s.width = width;
    s.cells = std::max(1, width / kCell);
    s.h.assign(s.cells, 0.0f);
    s.spillL = s.spillR = 0;
    Shape(s);
}

// Random terrain per drift: its own hills, slopes and smoothing
void Snow::Shape(Surface& s) {
    const float r = Cfg().drifts / 100.0f;
    const int n = s.cells;
    const bool g = s.ground;
    auto period = [](float lo, float hi) { return (int)RandRange(lo, hi); };
    std::vector<float> a, b, c;
    s.rough = r;

    // Height limit: large waves plus medium bumps
    SmoothNoise(a, n, g ? period(60, 150) : period(25, 70), 0, 1);
    SmoothNoise(b, n, g ? period(18, 45) : period(9, 24), 0, 1);
    s.cap.resize(n);
    for (int i = 0; i < n; ++i) s.cap[i] = 1.0f - r * 0.6f * (0.65f * a[i] + 0.35f * b[i]);

    // Where snow piles up faster: this is where hills grow
    SmoothNoise(a, n, g ? period(40, 130) : period(20, 55), 0, 1);
    SmoothNoise(b, n, g ? period(12, 30) : period(6, 16), 0, 1);
    s.dep.resize(n);
    for (int i = 0; i < n; ++i)
        s.dep[i] = std::max(0.12f, 1.0f + r * 1.5f * ((0.7f * a[i] + 0.3f * b[i]) * 2 - 1));

    // Slope and smoothing vary along the drift
    SmoothNoise(a, n, period(10, 40), 0, 1);
    SmoothNoise(c, n, period(8, 30), 0, 1);
    s.slope.resize(n);
    s.smooth.resize(n);
    for (int i = 0; i < n; ++i) {
        s.slope[i] = 1.0f + r * (a[i] * 1.1f - 0.45f);       // 0.55..1.65 at r = 1
        s.smooth[i] = 0.03f + 0.32f * c[i] * (1.0f - 0.5f * r); // smooth in places, lumpy in others
    }
}

void Snow::ComputeOcclusion(Surface& s, const World& w, int zLimit) {
    s.hideBelow.assign(s.cells, 0.0f);
    s.visCeil.assign(s.cells, 1e9f);
    const int y0 = s.top - 1;  // pixel right above the edge, where the snow lies
    for (int k = 0; k < zLimit && k < (int)w.wins.size(); ++k) {
        const RectI& o = w.wins[k].rc;
        if (o.r <= s.left || o.l >= s.left + s.width) continue;
        int c0 = std::max(0, (o.l - s.left) / kCell);
        int c1 = std::min(s.cells, (o.r - s.left + kCell - 1) / kCell);
        if (o.t <= y0 && o.b > y0) {
            // A window in front covers the edge: snow above its top edge still shows
            float below = (float)(s.top - o.t);
            for (int c = c0; c < c1; ++c) s.hideBelow[c] = std::max(s.hideBelow[c], below);
        } else if (o.b <= y0) {
            float ceil = (float)(s.top - o.b);
            for (int c = c0; c < c1; ++c) s.visCeil[c] = std::min(s.visCeil[c], ceil);
        }
    }
}

float Snow::ScaleAt(float x, float y) const {
    const MonitorInfo* best = nullptr;
    float bestDist = 1e9f;
    for (const auto& m : mons_) {
        float dx = x < m.rc.l ? m.rc.l - x : (x >= m.rc.r ? x - m.rc.r : 0);
        float dy = y < m.rc.t ? m.rc.t - y : (y >= m.rc.b ? y - m.rc.b : 0);
        if (dx + dy < bestDist) { bestDist = dx + dy; best = &m; }
    }
    return best ? best->scale : scale_;
}

void Snow::Sync(const World& w) {
    const Settings& c = Cfg();
    mons_ = w.monitors;
    const bool behind = c.mode == MODE_BEHIND;
    std::vector<Surface> next;
    next.reserve(surfaces.size() + 4);

    // Average snow on windows: popups such as Start and menus start with that much
    float avgPile = 0;
    int pileCount = 0;
    for (const auto& s : surfaces) {
        if (s.ground || s.cells == 0) continue;
        float sum = 0;
        for (float v : s.h) sum += v;
        if (sum > 0) { avgPile += sum / s.cells; ++pileCount; }
    }
    if (pileCount) avgPile /= pileCount;

    auto take = [&](uintptr_t key, bool ground, int left, int top, int width, float maxH, int zLimit,
                    int anchorL, int anchorR, float sc, bool transient) {
        Surface* old = nullptr;
        for (auto& s : surfaces)
            if (s.key == key) { old = &s; break; }
        Surface s;
        if (!old) {
            InitSurface(s, key, ground, left, top, width);
            if (transient && avgPile > 0) {  // Start and menus live for seconds: give them a light cap right away
                float level = Clampf(avgPile, 3 * sc, maxH * 0.4f);
                float wt = Clampf(maxH * 1.2f, 12.0f * sc, 130.0f * sc);
                for (int c = 0; c < s.cells; ++c) {
                    float t = std::min(1.0f, std::min(c + 0.5f, s.cells - c - 0.5f) * kCell / wt);
                    s.h[c] = level * (0.6f + 0.4f * s.cap[c]) * t * t * (3 - 2 * t);
                }
            }
        } else if (old->left == left && old->top == top && old->width == width) {
            s = std::move(*old);
        } else {
            Follow(s, *old, left, top, width, anchorL, anchorR);  // the window is moving: its snow rides along
        }
        if (old) old->key = 0;
        s.anchorL = anchorL;
        s.anchorR = anchorR;
        if (s.rough != c.drifts / 100.0f) Shape(s);
        s.maxH = maxH;
        s.scale = sc;
        s.lastSync = time_;
        s.z = zLimit;
        ComputeOcclusion(s, w, zLimit);
        next.push_back(std::move(s));
    };

    const int virtW = w.virt.w();
    if (c.onTaskbar) {
        int zGround = behind ? (int)w.wins.size() : std::max(0, w.firstTaskbarZ);
        for (size_t i = 0; i < w.monitors.size(); ++i) {
            const MonitorInfo& m = w.monitors[i];
            take(kGroundKeyBase + i, true, m.rc.l, m.groundY, m.rc.w(), c.pileGround * m.scale, zGround, m.rc.l, m.rc.r,
                 m.scale, false);
        }
    }
    if (c.onWindows) {
        for (size_t z = 0; z < w.wins.size(); ++z) {
            const WinInfo& wi = w.wins[z];
            if (wi.taskbar) continue;
            const MonitorInfo* mon = nullptr;
            int cx = (wi.rc.l + wi.rc.r) / 2;
            for (auto& m : w.monitors)
                if (cx >= m.rc.l && cx < m.rc.r && wi.rc.t >= m.rc.t && wi.rc.t < m.rc.b) { mon = &m; break; }
            if (!mon) continue;
            if (wi.rc.t <= mon->rc.t + 2 || wi.rc.t >= mon->groundY - 2) continue;  // maximized, or below the taskbar
            const int inset = (int)(7 * mon->scale);  // Windows 11 rounded corners
            int left = std::max(0, wi.rc.l + inset);
            int right = std::min(virtW, wi.rc.r - inset);
            if (right - left < 40) continue;
            float maxH = std::min(c.pileWindows * mon->scale, (float)(wi.rc.t - mon->rc.t));
            take((uintptr_t)wi.hwnd, false, left, wi.rc.t, right - left, maxH, (int)z, wi.rc.l, wi.rc.r, mon->scale,
                 wi.transient);
        }
    }
    // Windows that are gone (closed or minimized): their snow just disappears
    surfaces = std::move(next);
}

void Snow::SpawnChunk(float x, float y, float vx, float vy, float mass) {
    if (flakes.size() > 9000) return;
    Flake f{};
    f.x = x; f.y = y; f.vx = vx; f.vy = vy;
    f.mass = mass;
    f.size = std::max(1.6f, std::sqrt(mass) * 0.9f);
    f.z = 1.0f;
    f.alpha = 0.95f;
    f.kind = FLAKE_CHUNK;
    f.s = ScaleAt(x, y);
    flakes.push_back(f);
}

void Snow::ShakeOff(const Surface& s, int maxChunks) {
    double total = 0;
    for (float v : s.h) total += v;
    total *= kCell;
    if (total < 1) return;
    int count = (int)std::clamp(total / ChunkMass(s.scale), 1.0, (double)maxChunks);
    float mass = (float)(total / count);
    float acc = Rand01() * mass;
    for (int c = 0; c < s.cells; ++c) {
        acc += s.h[c] * kCell;
        while (acc >= mass) {
            acc -= mass;
            SpawnChunk(s.left + (c + Rand01()) * kCell, s.top - s.h[c] * Rand01(),
                       RandRange(-30, 30) * s.scale, RandRange(-30, 30) * s.scale, mass);
        }
    }
}

// Shake off a share of the snow (fast window move). vx, vy: initial clump velocity
void Snow::ShakePart(Surface& s, float frac, float vx, float vy) {
    const float m = ChunkMass(s.scale);
    float acc = 0;
    for (int c = 0; c < s.cells; ++c) {
        float take = s.h[c] * frac;
        if (take <= 0) continue;
        s.h[c] -= take;
        acc += take * kCell;
        while (acc >= m) {
            acc -= m;
            SpawnChunk(s.left + (c + Rand01()) * kCell, s.top - s.h[c] - Rand01() * take,
                       vx + RandRange(-40, 40) * s.scale, vy - RandRange(0, 60) * s.scale, m);
        }
    }
}

// Window moved or resized: snow stays on it, the excess falls off
void Snow::Follow(Surface& s, Surface& old, int left, int top, int width, int anchorL, int anchorR) {
    float dt = std::max(0.008f, time_ - old.lastSync);
    // Same real window width means a move, even if the screen edge clips the window
    const bool moved = anchorR - anchorL == old.anchorR - old.anchorL;
    const int shift = moved ? anchorL - old.anchorL : 0;
    float dx = (float)shift, dy = (float)(top - old.top);
    if (moved && old.width == width) {
        s = std::move(old);
        s.left = left;
        s.top = top;
    } else {
        // Snow moves with the window (or stays put on resize),
        // and whatever ends up past the edge falls
        InitSurface(s, old.key, old.ground, left, top, width);
        const float m = ChunkMass(old.scale);
        float acc = 0;
        if (!moved) dy = 0;
        for (int oc = 0; oc < old.cells; ++oc) {
            float x = old.left + (oc + 0.5f) * kCell;
            int c = (int)std::floor((x + shift - left) / kCell);
            if (c >= 0 && c < s.cells) {
                s.h[c] = std::max(s.h[c], old.h[oc]);
                continue;
            }
            acc += old.h[oc] * kCell;
            while (acc >= m) {
                acc -= m;
                SpawnChunk(x + shift, top - old.h[oc] * Rand01(), RandRange(-20, 20) * old.scale, 0, m);
            }
        }
    }
    // Slow drag: snow rides along. Fast shake: some of it flies off
    float speed = std::sqrt(dx * dx + dy * dy) / dt;
    float frac = Clampf((speed - 900 * old.scale) / (6000 * old.scale), 0.0f, 0.35f);
    if (frac > 0) ShakePart(s, frac, dx / dt * 0.3f, dy / dt * 0.3f);
}

void Snow::ShakeAll() {
    for (auto& s : surfaces) {
        ShakeOff(s, 700);
        std::fill(s.h.begin(), s.h.end(), 0.0f);
    }
}

void Snow::ClearAll() {
    flakes.clear();
    for (auto& s : surfaces) std::fill(s.h.begin(), s.h.end(), 0.0f);
    prefilled_ = false;
}

void Snow::Land(const Flake& f, Surface& s, int cell) {
    float area = f.kind == FLAKE_CHUNK ? f.mass : f.mass * accumK_ * s.dep[cell];
    if (area <= 0) return;
    int spread = std::max(1, (int)(f.size * 0.5f / kCell) + 1);
    float wsum = 0;
    for (int d = -spread; d <= spread; ++d) wsum += (float)(spread + 1 - std::abs(d));
    for (int d = -spread; d <= spread; ++d) {
        int c = cell + d;
        if (c < 0 || c >= s.cells) continue;
        float dh = area / kCell * (spread + 1 - std::abs(d)) / wsum;
        s.h[c] = std::min(s.h[c] + dh, std::max(s.h[c], s.maxH * s.cap[c]));
    }
}

void Snow::Relax(Surface& s, float dt) {
    const int n = s.cells;
    if (n < 2) return;
    auto& h = s.h;
    const float base = kCell * 1.05f;  // angle of repose, about 46°
    relaxDir_ = !relaxDir_;
    for (int i = 0; i + 1 < n; ++i) {
        int a = relaxDir_ ? i : n - 2 - i;
        float lim = base * s.slope[a];
        float d = h[a] - h[a + 1];
        if (d > lim)       { float t = (d - lim) * 0.5f; h[a] -= t; h[a + 1] += t; }
        else if (d < -lim) { float t = (-d - lim) * 0.5f; h[a] += t; h[a + 1] -= t; }
    }
    // Smoothing: stronger in some places, lumpy in others
    const float k = std::min(1.0f, dt * 30.0f) * 0.5f;
    for (int i = 0; i + 1 < n; ++i) {
        float f = (h[i] - h[i + 1]) * s.smooth[i] * k;
        h[i] -= f;
        h[i + 1] += f;
    }
    float melt = Cfg().melt / 100.0f * 0.6f * s.scale * dt;
    if (melt > 0)
        for (auto& v : h) v = std::max(0.0f, v - melt);
    if (s.ground) return;

    // Window edges: the drift tapers off toward the corner like snow on a roof edge, no wall.
    // Anything above that slope slides off and rolls to the corner
    const float wt = Clampf(s.maxH * 1.2f, 12.0f * s.scale, 130.0f * s.scale);
    for (int i = 0; i < n / 2; ++i) {
        float t = (i + 0.5f) * kCell / wt;
        if (t >= 1) break;
        float cap = s.maxH * t * t * (3 - 2 * t);
        if (h[i] > cap) { s.spillL += (h[i] - cap) * kCell; h[i] = cap; }
        int j = n - 1 - i;
        if (h[j] > cap) { s.spillR += (h[j] - cap) * kCell; h[j] = cap; }
    }
    const float m = ChunkMass(s.scale);
    while (s.spillL >= m) { float mass = m * RandRange(0.7f, 1.6f); s.spillL -= mass; SpawnSlide(s, -1, mass); }
    while (s.spillR >= m) { float mass = m * RandRange(0.7f, 1.6f); s.spillR -= mass; SpawnSlide(s, 1, mass); }
}

// A clump appears on the slope near the edge and rolls toward the corner
void Snow::SpawnSlide(const Surface& s, int dir, float mass) {
    if (flakes.size() > 9000) return;
    const float wt = Clampf(s.maxH * 1.2f, 12.0f * s.scale, 130.0f * s.scale);
    float d = RandRange(0.35f, 0.9f) * std::min(wt, s.width * 0.5f);
    Flake f{};
    f.kind = FLAKE_SLIDE;
    f.x = dir < 0 ? s.left + d : s.left + s.width - d;
    f.mass = mass;
    f.size = std::max(1.8f, std::sqrt(mass) * 0.95f);
    f.s = s.scale;
    f.z = 1;
    f.alpha = 0.95f;
    f.dir = (int8_t)dir;
    f.surf = s.key;
    f.vx = dir * RandRange(8, 22) * s.scale;
    int cell = std::clamp((int)((f.x - s.left) / kCell), 0, s.cells - 1);
    f.y = s.top - s.h[cell] - f.size * 0.4f;
    flakes.push_back(f);
}

// Rolls along the surface, speeding up; past the window edge it becomes a falling clump
bool Snow::MoveSlide(Flake& f, float dt) {
    const Surface* s = nullptr;
    for (const auto& c : surfaces)
        if (c.key == f.surf) { s = &c; break; }
    if (!s) return false;
    float speed = std::min(std::fabs(f.vx) + 160 * f.s * dt, 260 * f.s);
    f.vx = f.dir * speed;
    f.x += f.vx * dt;
    if (f.x < s->left || f.x >= s->left + s->width) return false;
    int cell = std::clamp((int)((f.x - s->left) / kCell), 0, s->cells - 1);
    f.y = s->top - s->h[cell] - f.size * 0.4f;
    f.rot += f.dir * speed / std::max(1.0f, f.size) * dt;
    return true;
}

void Snow::Knock(float dt, POINT cur) {
    const float sc = ScaleAt((float)cur.x, (float)cur.y);
    const float r = Cfg().cursorRadius * sc;
    const float m = ChunkMass(sc);
    for (auto& s : surfaces) {
        if (cur.x + r < s.left || cur.x - r > s.left + s.width) continue;
        if (cur.y > s.top + 2 || cur.y < s.top - s.maxH - r) continue;
        int c0 = std::max(0, (int)((cur.x - r - s.left) / kCell));
        int c1 = std::min(s.cells - 1, (int)((cur.x + r - s.left) / kCell));
        float acc = 0;
        for (int c = c0; c <= c1; ++c) {
            const float hb = s.hideBelow[c];
            if (s.h[c] <= hb + 0.05f) continue;  // no visible snow in this cell
            float cx = s.left + (c + 0.5f) * kCell;
            float dx = cx - cur.x;
            if (std::fabs(dx) >= r) continue;
            float span = std::sqrt(r * r - dx * dx);
            if (cur.y - span >= s.top - hb) continue;  // cursor is on the window in front, below the visible snow
            float newH = Clampf(s.top - (cur.y + span), hb, s.h[c]);
            float removed = s.h[c] - newH;
            if (removed < 0.05f) continue;
            s.h[c] = newH;
            acc += removed * kCell;
            while (acc >= m) {
                acc -= m;
                SpawnChunk(cx, s.top - newH - Rand01() * removed,
                           curVx_ * 0.35f + RandRange(-50, 50) * sc,
                           curVy_ * 0.35f - RandRange(40, 170) * sc, m);
            }
        }
    }
    (void)dt;
}
