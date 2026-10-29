#include "snowui.h"
#include "ui.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>

static const wchar_t kPanelClass[] = L"LayerSnow.Panel";
static const wchar_t kPopupClass[] = L"LayerSnow.Flyout";
static constexpr float kPi = 3.14159265f;

static D2D1_COLOR_F Rgb(int r, int g, int b, float a = 1) { return D2D1::ColorF(r / 255.f, g / 255.f, b / 255.f, a); }

static bool SystemUsesDarkTheme() {
    DWORD light = 1, size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    return light == 0;
}

// Flat palettes: no gradients, one icy accent.
static Palette MakePalette(bool dark) {
    Palette p;
    p.dark = dark;
    if (dark) {
        p.bg = Rgb(17, 22, 31);        p.card = Rgb(23, 30, 42);      p.border = Rgb(36, 45, 60);
        p.text = Rgb(232, 238, 246);   p.dim = Rgb(140, 152, 168);    p.accent = Rgb(106, 174, 245);
        p.onAccent = Rgb(12, 18, 30);  p.track = Rgb(42, 52, 70);     p.thumb = Rgb(240, 245, 251);
        p.segBg = Rgb(28, 36, 51);     p.segSel = Rgb(44, 56, 76);    p.hover = Rgb(32, 41, 57);
        p.flake = Rgb(200, 215, 235, 0.35f);
        p.hillBack = Rgb(24, 31, 44);  p.hillFront = Rgb(29, 38, 54);
        p.caption = RGB(17, 22, 31);   p.captionText = RGB(232, 238, 246);
    } else {
        p.bg = Rgb(246, 248, 251);     p.card = Rgb(255, 255, 255);   p.border = Rgb(228, 233, 241);
        p.text = Rgb(28, 36, 48);      p.dim = Rgb(107, 118, 134);    p.accent = Rgb(66, 140, 226);
        p.onAccent = Rgb(255, 255, 255); p.track = Rgb(226, 232, 240); p.thumb = Rgb(255, 255, 255);
        p.segBg = Rgb(237, 241, 246);  p.segSel = Rgb(255, 255, 255); p.hover = Rgb(236, 241, 248);
        p.flake = Rgb(150, 175, 205, 0.45f);
        p.hillBack = Rgb(234, 239, 246); p.hillFront = Rgb(240, 244, 249);
        p.caption = RGB(246, 248, 251); p.captionText = RGB(28, 36, 48);
    }
    return p;
}

SnowPanel::~SnowPanel() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
}

// ---------- Creation ----------

bool SnowPanel::Create(const wchar_t* title, float width, float height, Handler onEvent, const POINT* anchor) {
    w_ = width;
    h_ = height;
    onEvent_ = std::move(onEvent);
    popup_ = anchor != nullptr;
    if (anchor) anchor_ = *anchor;
    pal_ = MakePalette(SystemUsesDarkTheme());

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.lpfnWndProc = Proc;
        wc.hInstance = AppInstance();
        wc.hIcon = AppIcon(false);
        wc.hIconSm = AppIcon(true);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kPanelClass;
        RegisterClassExW(&wc);
        // Windows 10 gives borderless popups no DWM shadow, so use the classic one there.
        wc.style = WindowsBuild() < 22000 ? CS_DROPSHADOW : 0;
        wc.lpszClassName = kPopupClass;
        RegisterClassExW(&wc);
        registered = true;
    }
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dw_.GetAddressOf()))))
        return false;
    auto fmt = [&](float size, DWRITE_FONT_WEIGHT wt, DWRITE_TEXT_ALIGNMENT ta, bool wrap,
                   ComPtr<IDWriteTextFormat>& out) {
        dw_->CreateTextFormat(L"Segoe UI", nullptr, wt, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                              L"", &out);
        out->SetTextAlignment(ta);
        out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (!wrap) out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    };
    fmt(26, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false, fTitle_);
    fmt(13.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false, fSub_);
    fmt(14.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false, fCard_);
    fmt(13.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true, fLabel_);
    fmt(13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING, false, fValue_);
    fmt(13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, false, fButton_);
    fmt(12, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false, fCaption_);
    fmt(12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, false, fSeg_);
    d2d_->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                        D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                            nullptr, 0, &round_);
    BuildHills();

    const int count = std::max(14, (int)(w_ * h_ / 12000));
    for (int i = 0; i < count; ++i) {
        PanelFlake f;
        f.x = RandRange(0, w_);
        f.y = RandRange(0, h_);
        f.r = RandRange(0.8f, 2.2f);
        f.a = RandRange(0.5f, 1.0f);
        f.vy = 8 + f.r * 7;
        f.phase = RandRange(0, 2 * kPi);
        f.sway = RandRange(5, 14);
        flakes_.push_back(f);
    }

    DWORD style = popup_ ? WS_POPUP : WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    DWORD exStyle = popup_ ? WS_EX_TOOLWINDOW | WS_EX_TOPMOST : 0;
    hwnd_ = CreateWindowExW(exStyle, popup_ ? kPopupClass : kPanelClass, title, style, CW_USEDEFAULT, CW_USEDEFAULT,
                            100, 100, nullptr, nullptr, AppInstance(), this);
    if (!hwnd_) return false;
    // Window chrome in the panel colors (Windows 11; older builds ignore these attributes).
    BOOL dark = pal_.dark;
    DwmSetWindowAttribute(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    if (popup_) {
        int corners = 2;  // DWMWCP_ROUND
        COLORREF border = pal_.dark ? RGB(36, 45, 60) : RGB(222, 228, 237);
        DwmSetWindowAttribute(hwnd_, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corners, sizeof(corners));
        DwmSetWindowAttribute(hwnd_, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof(border));
    } else {
        DwmSetWindowAttribute(hwnd_, 35 /*DWMWA_CAPTION_COLOR*/, &pal_.caption, sizeof(pal_.caption));
        DwmSetWindowAttribute(hwnd_, 36 /*DWMWA_TEXT_COLOR*/, &pal_.captionText, sizeof(pal_.captionText));
    }
    Fit();
    QueryPerformanceFrequency(&freq_);
    QueryPerformanceCounter(&last_);
    SetTimer(hwnd_, 1, 33, nullptr);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    return true;
}

// Size for the monitor DPI; shrink if the panel does not fit the work area.
void SnowPanel::Fit() {
    POINT pt;
    GetCursorPos(&pt);
    if (popup_) pt = anchor_;
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    UINT dpi = 96, dy = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpi, &dy);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    const RECT& wa = mi.rcWork;
    if (popup_) {
        // Above the cursor when the taskbar is at the bottom, below it otherwise, always inside the screen.
        scale_ = dpi / 96.0f;
        int ww = (int)std::lround(w_ * scale_), wh = (int)std::lround(h_ * scale_), gap = (int)(10 * scale_);
        int x = std::clamp((int)pt.x - ww / 2, (int)wa.left + gap, (int)wa.right - gap - ww);
        int y = std::min((int)pt.y, (int)wa.bottom) - wh - gap;
        if (y < wa.top + gap) y = std::max((int)pt.y, (int)wa.top) + gap;
        SetWindowPos(hwnd_, HWND_TOPMOST, x, y, ww, wh, SWP_NOACTIVATE);
    } else {
        DWORD style = (DWORD)GetWindowLongW(hwnd_, GWL_STYLE);
        RECT probe = {0, 0, 100, 100};
        AdjustWindowRectExForDpi(&probe, style, FALSE, 0, dpi);
        float frame = (float)((probe.bottom - probe.top) - 100);
        float workW = (float)(wa.right - wa.left), workH = (float)(wa.bottom - wa.top);
        scale_ = std::min({dpi / 96.0f, (workH - frame - 16) / h_, (workW - 32) / w_});
        RECT rc = {0, 0, (LONG)std::lround(w_ * scale_), (LONG)std::lround(h_ * scale_)};
        AdjustWindowRectExForDpi(&rc, style, FALSE, 0, dpi);
        int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
        SetWindowPos(hwnd_, nullptr, wa.left + ((LONG)workW - ww) / 2, wa.top + ((LONG)workH - wh) / 2, ww, wh,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (rt_) rt_->SetDpi(96 * scale_, 96 * scale_);
}

void SnowPanel::BuildHills() {
    auto hills = [&](ComPtr<ID2D1PathGeometry>& g, float base, float a1, float f1, float a2, float f2, float ph) {
        d2d_->CreatePathGeometry(&g);
        ComPtr<ID2D1GeometrySink> k;
        g->Open(&k);
        k->BeginFigure({0, h_ + 2}, D2D1_FIGURE_BEGIN_FILLED);
        for (float x = 0; x <= w_ + 8; x += 8)
            k->AddLine({x, h_ - base - a1 * std::sin(x * f1 + ph) - a2 * std::sin(x * f2 + ph * 2.3f)});
        k->AddLine({w_ + 8, h_ + 2});
        k->EndFigure(D2D1_FIGURE_END_CLOSED);
        k->Close();
    };
    hills(hillBack_, 24, 8, 0.011f, 4, 0.029f, 1.0f);
    hills(hillFront_, 12, 6, 0.0085f, 3, 0.033f, 2.4f);
}

bool SnowPanel::CreateResources() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    if (FAILED(d2d_->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
                                            D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(rc.right, rc.bottom)),
                                            &rt_)))
        return false;
    rt_->SetDpi(96 * scale_, 96 * scale_);
    rt_->CreateSolidColorBrush(pal_.text, &brush_);
    return brush_ != nullptr;
}

// ---------- Widgets ----------

void SnowPanel::AddCard(D2D1_RECT_F rc, const std::wstring& title) { widgets_.push_back({W_CARD, rc, title}); }
void SnowPanel::AddText(D2D1_RECT_F rc, const std::wstring& text) { widgets_.push_back({W_TEXT, rc, text}); }
void SnowPanel::AddCaption(D2D1_RECT_F rc, const std::wstring& text, bool right) {
    Widget w{W_CAPTION, rc, text};
    w.right = right;
    widgets_.push_back(w);
}

void SnowPanel::AddLink(D2D1_RECT_F rc, const std::wstring& text, const std::wstring& url, bool right) {
    Widget w{W_LINK, rc, text};
    w.url = url;
    w.right = right;
    widgets_.push_back(w);
}

void SnowPanel::AddItem(D2D1_RECT_F rc, const std::wstring& text, int id) {
    Widget w{W_ITEM, rc, text};
    w.id = id;
    widgets_.push_back(w);
}

void SnowPanel::AddSlider(D2D1_RECT_F rc, const std::wstring& label, int lo, int hi, const std::wstring& unit,
                          std::function<int()> get, std::function<void(int)> set) {
    Widget w{W_SLIDER, rc, label, unit};
    w.lo = lo;
    w.hi = hi;
    w.get = std::move(get);
    w.set = std::move(set);
    widgets_.push_back(w);
}

void SnowPanel::AddToggle(D2D1_RECT_F rc, const std::wstring& label, std::function<int()> get,
                          std::function<void(int)> set) {
    Widget w{W_TOGGLE, rc, label};
    w.get = std::move(get);
    w.set = std::move(set);
    widgets_.push_back(w);
}

void SnowPanel::AddButton(D2D1_RECT_F rc, const std::wstring& text, int id, bool primary) {
    Widget w{W_BUTTON, rc, text};
    w.id = id;
    w.primary = primary;
    widgets_.push_back(w);
}

void SnowPanel::AddSegment(D2D1_RECT_F rc, std::vector<std::wstring> items, std::function<int()> get,
                           std::function<void(int)> set) {
    Widget w{W_SEGMENT, rc};
    w.items = std::move(items);
    w.get = std::move(get);
    w.set = std::move(set);
    widgets_.push_back(w);
}

D2D1_RECT_F SnowPanel::SliderTrack(const Widget& w) const {
    float cy = (w.rc.top + w.rc.bottom) / 2;
    return {w.rc.left + 176, cy - 2, w.rc.right - 62, cy + 2};
}

void SnowPanel::SetSliderFromX(Widget& w, float x) {
    D2D1_RECT_F t = SliderTrack(w);
    float k = Clampf((x - t.left) / (t.right - t.left), 0, 1);
    int v = w.lo + (int)std::lround(k * (w.hi - w.lo));
    if (v != w.get()) {
        w.set(v);
        if (onEvent_) onEvent_(0);
        Redraw();
    }
}

int SnowPanel::HitTest(float x, float y) const {
    for (int i = (int)widgets_.size() - 1; i >= 0; --i) {
        const Widget& w = widgets_[i];
        if (w.type == W_CARD || w.type == W_TEXT || w.type == W_CAPTION) continue;
        D2D1_RECT_F r = w.rc;
        if (w.type == W_SLIDER) {
            D2D1_RECT_F t = SliderTrack(w);
            r = {t.left - 10, r.top, t.right + 10, r.bottom};
        }
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return i;
    }
    return -1;
}

// ---------- Drawing ----------

void SnowPanel::Text(const std::wstring& s, IDWriteTextFormat* f, D2D1_RECT_F rc, D2D1_COLOR_F c) {
    brush_->SetColor(c);
    rt_->DrawText(s.c_str(), (UINT32)s.size(), f, rc, brush_.Get());
}

void SnowPanel::DrawFlakeIcon(float cx, float cy, float r, D2D1_COLOR_F color, float width) {
    brush_->SetColor(color);
    for (int k = 0; k < 6; ++k) {
        float a = kPi / 2 + k * kPi / 3, dx = std::cos(a), dy = -std::sin(a);
        rt_->DrawLine({cx, cy}, {cx + dx * r, cy + dy * r}, brush_.Get(), width, round_.Get());
        float bx = cx + dx * r * 0.55f, by = cy + dy * r * 0.55f;
        for (int s = -1; s <= 1; s += 2) {
            float b = a + s * 0.85f;
            rt_->DrawLine({bx, by}, {bx + std::cos(b) * r * 0.32f, by - std::sin(b) * r * 0.32f}, brush_.Get(),
                          width * 0.85f, round_.Get());
        }
    }
}

void SnowPanel::DrawBackground() {
    rt_->Clear(pal_.bg);
    for (const PanelFlake& f : flakes_) {
        D2D1_COLOR_F c = pal_.flake;
        c.a *= f.a;
        brush_->SetColor(c);
        rt_->FillEllipse(D2D1::Ellipse({f.x, f.y}, f.r, f.r), brush_.Get());
    }
    brush_->SetColor(pal_.hillBack);
    rt_->FillGeometry(hillBack_.Get(), brush_.Get());
    brush_->SetColor(pal_.hillFront);
    rt_->FillGeometry(hillFront_.Get(), brush_.Get());
    if (popup_) {
        DrawFlakeIcon(27, 32, 8, pal_.accent, 1.5f);
        Text(title_, fCard_.Get(), {44, 18, w_ - 16, 46}, pal_.text);
    } else {
        DrawFlakeIcon(42, 48, 13, pal_.accent, 2.0f);
        Text(title_, fTitle_.Get(), {66, 28, w_ - 20, 66}, pal_.text);
        Text(subtitle_, fSub_.Get(), {67, 64, w_ - 20, 86}, pal_.dim);
    }
}

void SnowPanel::DrawWidget(const Widget& w, bool hot) {
    const D2D1_RECT_F& r = w.rc;
    const float cy = (r.top + r.bottom) / 2;
    switch (w.type) {
    case W_CARD: {
        D2D1_ROUNDED_RECT rr = {r, 14, 14};
        brush_->SetColor(pal_.card);
        rt_->FillRoundedRectangle(rr, brush_.Get());
        brush_->SetColor(pal_.border);
        rt_->DrawRoundedRectangle(rr, brush_.Get(), 1);
        Text(w.text, fCard_.Get(), {r.left + 20, r.top + 12, r.right - 16, r.top + 38}, pal_.text);
        break;
    }
    case W_TEXT:
        fLabel_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        Text(w.text, fLabel_.Get(), r, pal_.dim);
        fLabel_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        break;
    case W_CAPTION:
        if (w.right) fCaption_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        Text(w.text, fCaption_.Get(), r, pal_.dim);
        fCaption_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        break;
    case W_LINK: {
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dw_->CreateTextLayout(w.text.c_str(), (UINT32)w.text.size(), fCaption_.Get(), r.right - r.left,
                                         r.bottom - r.top, &layout)))
            break;
        layout->SetTextAlignment(w.right ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
        layout->SetUnderline(hot, {0, (UINT32)w.text.size()});
        brush_->SetColor(pal_.accent);
        rt_->DrawTextLayout({r.left, r.top}, layout.Get(), brush_.Get());
        break;
    }
    case W_ITEM:
        if (hot) {
            brush_->SetColor(pal_.hover);
            rt_->FillRoundedRectangle({r, 8, 8}, brush_.Get());
        }
        Text(w.text, fLabel_.Get(), {r.left + 12, r.top, r.right - 12, r.bottom}, pal_.text);
        break;
    case W_SLIDER: {
        int v = w.get();
        Text(w.text, fLabel_.Get(), {r.left, r.top, r.left + 172, r.bottom}, pal_.text);
        Text(std::to_wstring(v) + w.unit, fValue_.Get(), {r.right - 58, r.top, r.right, r.bottom}, pal_.dim);
        D2D1_RECT_F t = SliderTrack(w);
        float k = (float)(v - w.lo) / std::max(1, w.hi - w.lo);
        float x = t.left + (t.right - t.left) * k;
        brush_->SetColor(pal_.track);
        rt_->FillRoundedRectangle({t, 2, 2}, brush_.Get());
        brush_->SetColor(pal_.accent);
        rt_->FillRoundedRectangle({{t.left, t.top, x, t.bottom}, 2, 2}, brush_.Get());
        float rad = hot ? 8.5f : 7.5f;
        brush_->SetColor(pal_.thumb);
        rt_->FillEllipse(D2D1::Ellipse({x, cy}, rad, rad), brush_.Get());
        brush_->SetColor(pal_.accent);
        rt_->DrawEllipse(D2D1::Ellipse({x, cy}, rad, rad), brush_.Get(), 2);
        break;
    }
    case W_TOGGLE: {
        bool on = w.get() != 0;
        D2D1_ROUNDED_RECT sw = {{r.left, cy - 10, r.left + 36, cy + 10}, 10, 10};
        brush_->SetColor(on ? pal_.accent : pal_.track);
        rt_->FillRoundedRectangle(sw, brush_.Get());
        brush_->SetColor(pal_.thumb);
        rt_->FillEllipse(D2D1::Ellipse({on ? r.left + 26 : r.left + 10, cy}, 7, 7), brush_.Get());
        Text(w.text, fLabel_.Get(), {r.left + 48, r.top, r.right, r.bottom}, on ? pal_.text : pal_.dim);
        break;
    }
    case W_BUTTON: {
        float rad = (r.bottom - r.top) / 2;
        D2D1_ROUNDED_RECT rr = {r, rad, rad};
        bool down = hot && pressed_ >= 0;
        if (w.primary) {
            D2D1_COLOR_F c = pal_.accent;
            c.a = down ? 0.8f : (hot ? 0.92f : 1.0f);
            brush_->SetColor(c);
            rt_->FillRoundedRectangle(rr, brush_.Get());
            Text(w.text, fButton_.Get(), r, pal_.onAccent);
        } else {
            brush_->SetColor(hot ? pal_.hover : pal_.card);
            rt_->FillRoundedRectangle(rr, brush_.Get());
            brush_->SetColor(pal_.border);
            rt_->DrawRoundedRectangle(rr, brush_.Get(), 1);
            Text(w.text, fButton_.Get(), r, pal_.text);
        }
        break;
    }
    case W_SEGMENT: {
        brush_->SetColor(pal_.segBg);
        rt_->FillRoundedRectangle({r, 10, 10}, brush_.Get());
        int n = (int)w.items.size(), sel = w.get();
        float iw = (r.right - r.left) / n;
        for (int i = 0; i < n; ++i) {
            D2D1_RECT_F ir = {r.left + iw * i, r.top, r.left + iw * (i + 1), r.bottom};
            if (i == sel) {
                D2D1_ROUNDED_RECT sr = {{ir.left + 3, ir.top + 3, ir.right - 3, ir.bottom - 3}, 8, 8};
                brush_->SetColor(pal_.segSel);
                rt_->FillRoundedRectangle(sr, brush_.Get());
                brush_->SetColor(pal_.border);
                rt_->DrawRoundedRectangle(sr, brush_.Get(), 1);
            }
            Text(w.items[i], n > 3 ? fSeg_.Get() : fButton_.Get(), ir, i == sel ? pal_.text : pal_.dim);
        }
        break;
    }
    }
}

void SnowPanel::Paint() {
    if (!rt_ && !CreateResources()) return;
    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    DrawBackground();
    for (size_t i = 0; i < widgets_.size(); ++i) DrawWidget(widgets_[i], (int)i == hot_ || (int)i == drag_);
    if (popup_ && WindowsBuild() < 22000) {  // Windows 10 draws no border around popups
        brush_->SetColor(pal_.border);
        rt_->DrawRectangle({0.5f, 0.5f, w_ - 0.5f, h_ - 0.5f}, brush_.Get(), 1);
    }
    if (rt_->EndDraw() == D2DERR_RECREATE_TARGET) {
        rt_.Reset();
        brush_.Reset();
    }
}

void SnowPanel::Tick() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = std::min(0.1f, (float)(now.QuadPart - last_.QuadPart) / freq_.QuadPart);
    last_ = now;
    time_ += dt;
    if (popup_ && GetForegroundWindow() != hwnd_) {
        // Windows may refuse focus to the popup; then close it on any click outside.
        bool down = ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) &
                     0x8000) != 0;
        POINT pt;
        RECT rc;
        GetCursorPos(&pt);
        GetWindowRect(hwnd_, &rc);
        if (down && !PtInRect(&rc, pt)) {
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            return;
        }
    }
    for (auto& f : flakes_) {
        f.y += f.vy * dt;
        f.x += std::sin(time_ * 0.9f + f.phase) * f.sway * dt;
        if (f.y > h_ + 4) {
            f.y = -4;
            f.x = RandRange(0, w_);
        }
    }
    Redraw();
}

// ---------- Messages ----------

LRESULT CALLBACK SnowPanel::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        reinterpret_cast<SnowPanel*>(cs->lpCreateParams)->hwnd_ = hwnd;
    }
    auto* self = reinterpret_cast<SnowPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
        auto cb = self->onDestroy;  // the callback may delete the panel
        if (cb) cb();
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return self->Handle(msg, wp, lp);
}

LRESULT SnowPanel::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    auto mx = [&] { return GET_X_LPARAM(lp) / scale_; };
    auto my = [&] { return GET_Y_LPARAM(lp) / scale_; };
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd_, &ps);
        Paint();
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (rt_) rt_->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
        return 0;
    case WM_DPICHANGED: {
        RECT* r = (RECT*)lp;
        scale_ = HIWORD(wp) / 96.0f;
        RECT rc = {0, 0, (LONG)std::lround(w_ * scale_), (LONG)std::lround(h_ * scale_)};
        AdjustWindowRectExForDpi(&rc, (DWORD)GetWindowLongW(hwnd_, GWL_STYLE), FALSE, 0, HIWORD(wp));
        SetWindowPos(hwnd_, nullptr, r->left, r->top, rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (rt_) rt_->SetDpi(96 * scale_, 96 * scale_);
        return 0;
    }
    case WM_TIMER:
        if (!IsIconic(hwnd_)) Tick();
        return 0;
    case WM_ENTERSIZEMOVE:
    case WM_EXITSIZEMOVE:
        if (HWND app = FindWindowW(kMsgWndClass, nullptr))
            PostMessageW(app, WM_APP_MODALLOOP, msg == WM_ENTERSIZEMOVE, 0);
        return 0;
    case WM_ACTIVATE:
        if (popup_ && LOWORD(wp) == WA_INACTIVE) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        return 0;
    case WM_MOUSEMOVE: {
        if (drag_ >= 0) {
            SetSliderFromX(widgets_[drag_], mx());
            return 0;
        }
        int h = HitTest(mx(), my());
        if (h != hot_) {
            hot_ = h;
            Redraw();
        }
        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (drag_ < 0 && pressed_ < 0) {
            hot_ = -1;
            Redraw();
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && hot_ >= 0) {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        int i = HitTest(mx(), my());
        if (i < 0) return 0;
        Widget& w = widgets_[i];
        if (w.type == W_SLIDER) {
            drag_ = i;
            SetCapture(hwnd_);
            SetSliderFromX(w, mx());
        } else if (w.type == W_TOGGLE) {
            w.set(w.get() ? 0 : 1);
            if (onEvent_) onEvent_(0);
        } else if (w.type == W_SEGMENT) {
            int n = (int)w.items.size();
            int idx = std::clamp((int)((mx() - w.rc.left) / ((w.rc.right - w.rc.left) / n)), 0, n - 1);
            if (idx != w.get()) {
                w.set(idx);
                if (onEvent_) onEvent_(0);
            }
        } else if (w.type == W_BUTTON || w.type == W_ITEM || w.type == W_LINK) {
            pressed_ = i;
            SetCapture(hwnd_);
        }
        Redraw();
        return 0;
    }
    case WM_LBUTTONUP: {
        if (drag_ >= 0) {
            drag_ = -1;
            ReleaseCapture();
        }
        if (pressed_ >= 0) {
            int p = pressed_;
            pressed_ = -1;
            ReleaseCapture();
            if (HitTest(mx(), my()) == p) {
                if (widgets_[p].type == W_LINK)
                    ShellExecuteW(nullptr, L"open", widgets_[p].url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                else if (onEvent_)
                    onEvent_(widgets_[p].id);  // may close the window
            }
            return 0;
        }
        Redraw();
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd_, &pt);
        int i = HitTest(pt.x / scale_, pt.y / scale_);
        if (i >= 0 && widgets_[i].type == W_SLIDER) {
            Widget& w = widgets_[i];
            int step = std::max(1, (w.hi - w.lo) / 50) * (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1);
            w.set(std::clamp(w.get() + step, w.lo, w.hi));
            if (onEvent_) onEvent_(0);
            Redraw();
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) DestroyWindow(hwnd_);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) DestroyWindow(hwnd_);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd_, 1);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
