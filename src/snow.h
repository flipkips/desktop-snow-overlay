#pragma once
#include "common.h"
#include "settings.h"
#include "world.h"

constexpr int kCell = 2;  // drift cell width, px

// FLAKE_SLIDE: a clump rolling down a drift toward a window corner before it falls
enum FlakeKind : uint8_t { FLAKE_DOT, FLAKE_CRYSTAL, FLAKE_CHUNK, FLAKE_SLIDE };

struct Flake {
    float x, y, vx, vy;
    float size;    // diameter, px
    float z;       // depth 0.35..1, 1 is nearest to the viewer
    float phase, freq;
    float rot, vrot;
    float alpha;
    float mass;    // snow area (px²) added to a drift on landing
    FlakeKind kind;
    uint8_t variant;  // crystal kind
    float s;          // scale of the monitor it spawned on
    int8_t dir;       // FLAKE_SLIDE: -1 rolls left, 1 rolls right
    uintptr_t surf;   // FLAKE_SLIDE: drift it rolls on
};

constexpr int kCrystalVariants = 4;

// Drift on a window's top edge, or the ground above the taskbar
struct Surface {
    uintptr_t key = 0;   // window HWND or a monitor key
    bool ground = false;
    int left = 0, top = 0, width = 0, cells = 0;
    float maxH = 0;
    float scale = 1;     // scale of the monitor the drift is on
    std::vector<float> h;        // snow height per cell
    std::vector<float> cap;      // height limit variation
    std::vector<float> dep;      // where snow piles up faster (hills)
    std::vector<float> slope;    // local angle of repose
    std::vector<float> smooth;   // local smoothing strength
    float rough = -1;            // roughness the terrain was generated with
    // Visible band per cell, from hideBelow to visCeil (pixels above the edge).
    // hideBelow > 0: a window in front covers the edge, snow shows only above its top
    std::vector<float> hideBelow;
    std::vector<float> visCeil;  // above this a window in front covers the snow
    int z = 0;                   // z-order position, drawn back to front
    float spillL = 0, spillR = 0;
    float lastSync = 0;          // time of the last sync with the window
    int anchorL = 0, anchorR = 0; // real window edges, not clipped by the screen
};

class Snow {
public:
    std::vector<Flake> flakes;
    std::vector<Surface> surfaces;

    void Sync(const World& w);
    void Update(float dt, const World& w, POINT cursor, bool cursorValid);
    void ShakeAll();
    void ClearAll();
    bool Idle() const;
    bool VisibleMotion() const;  // something on screen is moving
    void SetScale(float s) { scale_ = s; }

private:
    void Spawn(float dt, const World& w);
    void SpawnFlake(const World& w, bool anywhere);
    void SpawnChunk(float x, float y, float vx, float vy, float mass);
    void SpawnSlide(const Surface& s, int dir, float mass);
    bool MoveSlide(Flake& f, float dt);  // false: the drift is gone
    void Land(const Flake& f, Surface& s, int cell);
    void Relax(Surface& s, float dt);
    void Knock(float dt, POINT cur);
    void ShakeOff(const Surface& s, int maxChunks);
    void ShakePart(Surface& s, float frac, float vx, float vy);
    void Follow(Surface& s, Surface& old, int left, int top, int width, int anchorL, int anchorR);
    void InitSurface(Surface& s, uintptr_t key, bool ground, int left, int top, int width);
    void Shape(Surface& s);
    void ComputeOcclusion(Surface& s, const World& w, int zLimit);
    static float ChunkMass(float sc) { return 6.0f * sc * sc; }
    float ScaleAt(float x, float y) const;  // scale of the monitor at this point

    float scale_ = 1.0f;  // primary monitor scale, used when no monitor matches
    std::vector<MonitorInfo> mons_;
    float time_ = 0;
    float angleDeg_ = 0;     // current wind angle (setting plus cursor)
    float cursorAngle_ = 0;  // cursor part of the angle, eased toward its target
    float spawnAcc_ = 0;
    float accumK_ = 1;
    float curVx_ = 0, curVy_ = 0;
    POINT prevCursor_ = {0, 0};
    bool havePrevCursor_ = false;
    bool prefilled_ = false;
    bool relaxDir_ = false;
};
