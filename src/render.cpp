#include "render.h"

bool Renderer::Init(HWND hwnd, int width, int height, float scale) {
    Shutdown();
    scale_ = scale;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                   D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
                               D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIFactory2> dxgiFactory;
    if (FAILED(d3d_.As(&dxgiDevice))) return false;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory)))) return false;

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = width;
    sd.Height = height;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    sd.Scaling = DXGI_SCALING_STRETCH;
    if (FAILED(dxgiFactory->CreateSwapChainForComposition(dxgiDevice.Get(), &sd, nullptr, &swap_))) return false;

    D2D1_FACTORY_OPTIONS fo = {};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo,
                                 reinterpret_cast<void**>(factory_.GetAddressOf()))))
        return false;
    if (FAILED(factory_->CreateDevice(dxgiDevice.Get(), &d2dDevice_))) return false;
    if (FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_))) return false;

    ComPtr<IDXGISurface2> surface;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(dc_->CreateBitmapFromDxgiSurface(surface.Get(), &bp, &target_))) return false;
    dc_->SetTarget(target_.Get());

    if (FAILED(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&dcomp_)))) return false;
    if (FAILED(dcomp_->CreateTargetForHwnd(hwnd, TRUE, &dcompTarget_))) return false;
    if (FAILED(dcomp_->CreateVisual(&visual_))) return false;
    visual_->SetContent(swap_.Get());
    dcompTarget_->SetRoot(visual_.Get());
    dcomp_->Commit();

    if (!CreateSprites()) { Shutdown(); return false; }
    return true;
}

void Renderer::Shutdown() {
    dot_.Reset(); outline_.Reset(); fill_.Reset();
    for (auto& c : crystals_) c.Reset();
    visual_.Reset(); dcompTarget_.Reset(); dcomp_.Reset();
    if (dc_) dc_->SetTarget(nullptr);
    target_.Reset(); dc_.Reset(); d2dDevice_.Reset(); factory_.Reset();
    swap_.Reset(); d3d_.Reset();
}

bool Renderer::CreateSprites() {
    // Soft dot: white core with a faint dark halo so snow stays visible on white backgrounds
    const int N = 32;
    std::vector<uint32_t> px(N * N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            float dx = (x + 0.5f) / N * 2 - 1, dy = (y + 0.5f) / N * 2 - 1;
            float r = std::sqrt(dx * dx + dy * dy);
            float aw = std::pow(Clampf(1.0f - r * 1.55f, 0, 1), 0.7f);
            aw = Clampf(aw * 1.6f, 0, 1);
            float ring = 0.42f * std::exp(-std::pow((r - 0.62f) / 0.2f, 2.0f));
            float ad = ring * (1 - aw);
            float a = aw + ad;
            float cr = aw * 1.0f + ad * 0.30f, cg = aw * 1.0f + ad * 0.38f, cb = aw * 1.0f + ad * 0.55f;
            auto b8 = [](float v) { return (uint32_t)(Clampf(v, 0, 1) * 255.0f + 0.5f); };
            px[y * N + x] = (b8(a) << 24) | (b8(cr) << 16) | (b8(cg) << 8) | b8(cb);
        }
    D2D1_BITMAP_PROPERTIES1 sp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(dc_->CreateBitmap(D2D1::SizeU(N, N), px.data(), N * 4, &sp, &dot_))) return false;

    // Four kinds of crystal flakes, drawn once into 64x64 textures
    const float S = 64, kPi = 3.14159265f;
    struct Style {
        int branches;
        float pos[3], len[3];
        float angle;     // branch angle to the arm, radians
        bool tipHex;     // hexagonal plates at the arm tips
        bool centerHex;  // hexagon in the center
    };
    static const Style styles[kCrystalVariants] = {
        {2, {0.45f, 0.72f}, {0.30f, 0.20f}, 0.84f, false, false},               // dendrite
        {3, {0.30f, 0.52f, 0.74f}, {0.24f, 0.20f, 0.13f}, 1.0f, false, false},  // fern
        {1, {0.55f}, {0.20f}, 0.9f, true, true},                                // star with plates
        {1, {0.80f}, {0.22f}, 0.65f, false, true},                              // simple
    };
    D2D1_BITMAP_PROPERTIES1 tp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1StrokeStyle> round;
    factory_->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                            D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                                nullptr, 0, &round);
    ComPtr<ID2D1SolidColorBrush> dark, white;
    dc_->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.30f, 0.48f, 0.45f), &dark);
    dc_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &white);

    auto hexagon = [&](float cx, float cy, float r, float rot, ID2D1Brush* brush, float w) {
        D2D1_POINT_2F p[6];
        for (int i = 0; i < 6; ++i)
            p[i] = {cx + r * std::cos(rot + i * kPi / 3), cy - r * std::sin(rot + i * kPi / 3)};
        for (int i = 0; i < 6; ++i) dc_->DrawLine(p[i], p[(i + 1) % 6], brush, w, round.Get());
    };
    for (int v = 0; v < kCrystalVariants; ++v) {
        const Style& st = styles[v];
        if (FAILED(dc_->CreateBitmap(D2D1::SizeU((UINT)S, (UINT)S), nullptr, 0, &tp, &crystals_[v]))) return false;
        dc_->SetTarget(crystals_[v].Get());
        dc_->BeginDraw();
        dc_->Clear(D2D1::ColorF(0, 0, 0, 0));
        auto paint = [&](ID2D1Brush* brush, float wMain, float wBranch) {
            const float c = S / 2, R = S * (st.tipHex ? 0.40f : 0.44f);
            for (int k = 0; k < 6; ++k) {
                float a = kPi / 2 + k * kPi / 3;
                float dx = std::cos(a), dy = -std::sin(a);
                dc_->DrawLine({c, c}, {c + dx * R, c + dy * R}, brush, wMain, round.Get());
                for (int j = 0; j < st.branches; ++j) {
                    float bx = c + dx * R * st.pos[j], by = c + dy * R * st.pos[j];
                    for (int s = -1; s <= 1; s += 2) {
                        float b = a + s * st.angle;
                        dc_->DrawLine({bx, by}, {bx + std::cos(b) * R * st.len[j], by - std::sin(b) * R * st.len[j]},
                                      brush, wBranch, round.Get());
                    }
                }
                if (st.tipHex) hexagon(c + dx * R * 1.08f, c + dy * R * 1.08f, R * 0.13f, a, brush, wBranch);
            }
            if (st.centerHex) hexagon(c, c, R * 0.22f, kPi / 6, brush, wBranch);
        };
        paint(dark.Get(), 5.2f, 4.4f);
        paint(white.Get(), 2.8f, 2.2f);
        HRESULT hr = dc_->EndDraw();
        dc_->SetTarget(target_.Get());
        if (FAILED(hr)) return false;
    }

    dc_->CreateSolidColorBrush(D2D1::ColorF(0.30f, 0.40f, 0.58f, 0.70f), &outline_);
    D2D1_GRADIENT_STOP stops[2] = {{0.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.98f)},
                                   {1.0f, D2D1::ColorF(0.84f, 0.89f, 0.97f, 0.98f)}};
    ComPtr<ID2D1GradientStopCollection> coll;
    dc_->CreateGradientStopCollection(stops, 2, &coll);
    dc_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {0, 1}), coll.Get(), &fill_);
    return outline_ && fill_;
}

void Renderer::DrawPiles(const Snow& snow) {
    std::vector<float> top, bot;
    std::vector<D2D1_POINT_2F> pts;
    std::vector<D2D1_QUADRATIC_BEZIER_SEGMENT> segs;
    // Back to front: snow on a nearer window covers snow on a farther one
    std::vector<const Surface*> order;
    for (const Surface& s : snow.surfaces) order.push_back(&s);
    std::stable_sort(order.begin(), order.end(), [](const Surface* a, const Surface* b) { return a->z > b->z; });

    for (const Surface* sp : order) {
        const Surface& s = *sp;
        const int n = s.cells;
        // Visible band: the top is the slightly blurred height, the bottom is the edge of a window in front
        top.resize(n);
        bot.resize(n);
        for (int c = 0; c < n; ++c) {
            float l = c > 0 ? s.h[c - 1] : s.h[c];
            float r = c + 1 < n ? s.h[c + 1] : s.h[c];
            top[c] = std::min((l + 2 * s.h[c] + r) * 0.25f, s.visCeil[c]);
            bot[c] = s.hideBelow[c];
        }
        auto visible = [&](int c) { return top[c] - bot[c] >= 0.3f; };
        auto bottomY = [&](int c) { return bot[c] > 0 ? s.top - bot[c] : s.top + 1.0f; };

        ComPtr<ID2D1PathGeometry> fillGeom, edgeGeom;
        if (FAILED(factory_->CreatePathGeometry(&fillGeom)) || FAILED(factory_->CreatePathGeometry(&edgeGeom))) return;
        ComPtr<ID2D1GeometrySink> fs, es;
        fillGeom->Open(&fs);
        edgeGeom->Open(&es);
        bool any = false;
        for (int c = 0; c < n;) {
            if (!visible(c)) { ++c; continue; }
            int start = c;
            while (c < n && visible(c)) ++c;
            int end = c;
            // Control points every other cell, quadratic curves through the midpoints
            pts.clear();
            for (int i = start; i < end; i += 2) pts.push_back({s.left + (i + 0.5f) * kCell, s.top - top[i]});
            if ((end - 1 - start) % 2) pts.push_back({s.left + (end - 0.5f) * kCell, s.top - top[end - 1]});
            segs.clear();
            for (size_t i = 1; i + 1 < pts.size(); ++i)
                segs.push_back({pts[i], {(pts[i].x + pts[i + 1].x) * 0.5f, (pts[i].y + pts[i + 1].y) * 0.5f}});
            const float x0 = (float)(s.left + start * kCell), x1 = (float)(s.left + end * kCell);
            auto outline = [&](ID2D1GeometrySink* k) {  // left side, top of the drift, right side
                k->AddLine({x0, pts.front().y});
                k->AddLine(pts.front());
                if (!segs.empty()) k->AddQuadraticBeziers(segs.data(), (UINT32)segs.size());
                k->AddLine(pts.back());
                k->AddLine({x1, pts.back().y});
                k->AddLine({x1, bottomY(end - 1)});
            };
            fs->BeginFigure({x0, bottomY(start)}, D2D1_FIGURE_BEGIN_FILLED);
            outline(fs.Get());
            for (int i = end - 2; i >= start; --i)  // back along the bottom edge, with steps
                if (bot[i] != bot[i + 1]) {
                    float xb = (float)(s.left + (i + 1) * kCell);
                    fs->AddLine({xb, bottomY(i + 1)});
                    fs->AddLine({xb, bottomY(i)});
                }
            fs->EndFigure(D2D1_FIGURE_END_CLOSED);
            es->BeginFigure({x0, bottomY(start)}, D2D1_FIGURE_BEGIN_HOLLOW);
            outline(es.Get());
            es->EndFigure(D2D1_FIGURE_END_OPEN);
            any = true;
        }
        fs->Close();
        es->Close();
        if (!any) continue;
        fill_->SetStartPoint({0, s.top - std::max(8.0f, s.maxH)});
        fill_->SetEndPoint({0, (float)s.top});
        dc_->DrawGeometry(edgeGeom.Get(), outline_.Get(), 1.6f * scale_);  // outline only on top and the sides
        dc_->FillGeometry(fillGeom.Get(), fill_.Get());
    }
}

void Renderer::DrawFlakes(const Snow& snow, const World& world, bool behind) {
    const D2D1_MATRIX_3X2_F identity = D2D1::Matrix3x2F::Identity();
    const bool falling = Cfg().showFalling;
    for (const Flake& f : snow.flakes) {
        if (!falling && (f.kind == FLAKE_DOT || f.kind == FLAKE_CRYSTAL)) continue;  // only drifts are shown
        if (behind) {  // "behind windows" mode: hide flakes covered by a window
            bool hidden = false;
            for (const auto& wi : world.wins)
                if (wi.rc.contains(f.x, f.y)) { hidden = true; break; }
            if (hidden) continue;
        }
        if (f.kind == FLAKE_CRYSTAL) {
            // Spin plus a squeeze along one axis, as if the flake turns in 3D
            float r = f.size * 0.5f;
            float tumble = 0.55f + 0.45f * std::fabs(std::cos(f.rot * 0.7f + f.phase));
            dc_->SetTransform(D2D1::Matrix3x2F::Scale(tumble, 1, {f.x, f.y}) *
                              D2D1::Matrix3x2F::Rotation(f.rot * 57.29578f, {f.x, f.y}));
            dc_->DrawBitmap(crystals_[f.variant % kCrystalVariants].Get(),
                            D2D1::RectF(f.x - r, f.y - r, f.x + r, f.y + r), f.alpha, D2D1_INTERPOLATION_MODE_LINEAR);
            dc_->SetTransform(identity);
        } else {
            float r = f.size * 0.8f;
            dc_->DrawBitmap(dot_.Get(), D2D1::RectF(f.x - r, f.y - r, f.x + r, f.y + r), f.alpha,
                            D2D1_INTERPOLATION_MODE_LINEAR);
        }
    }
}

bool Renderer::Draw(const Snow& snow, const World& world) {
    if (!dc_) return false;
    dc_->BeginDraw();
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());
    dc_->Clear(D2D1::ColorF(0, 0, 0, 0));
    DrawPiles(snow);
    DrawFlakes(snow, world, Cfg().mode == MODE_BEHIND);
    HRESULT hr = dc_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) return false;
    hr = swap_->Present(1, 0);
    return !(hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET);
}
