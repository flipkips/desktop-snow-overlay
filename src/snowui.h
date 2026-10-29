#pragma once
// Custom-drawn window used by the settings panel, the tray menu, the installer and the uninstaller.
#include "common.h"
#include <d2d1.h>
#include <dwrite.h>
#include <functional>
#include <memory>

enum WidgetType { W_CARD, W_SLIDER, W_TOGGLE, W_BUTTON, W_SEGMENT, W_TEXT, W_ITEM, W_CAPTION, W_LINK };

struct Widget {
    WidgetType type;
    D2D1_RECT_F rc;  // logical pixels (96 DPI)
    std::wstring text, unit;
    int id = 0, lo = 0, hi = 100;
    bool primary = false;
    bool right = false;  // right-aligned text (captions, links)
    std::wstring url;
    std::vector<std::wstring> items;
    std::function<int()> get;  // slider value, toggle 0/1 or selected segment
    std::function<void(int)> set;
};

struct Palette {
    D2D1_COLOR_F bg, card, border, text, dim, accent, onAccent, track, thumb, segBg, segSel, hover, flake, hillBack,
        hillFront;
    COLORREF caption, captionText;
    bool dark;
};

struct PanelFlake { float x, y, r, a, vy, phase, sway; };

class SnowPanel {
public:
    using Handler = std::function<void(int id)>;  // button id, or 0 when a value changed

    ~SnowPanel();
    // With an anchor the panel is a borderless popup near that point that closes when it loses focus.
    bool Create(const wchar_t* title, float width, float height, Handler onEvent, const POINT* anchor = nullptr);
    HWND Hwnd() const { return hwnd_; }
    void Redraw() { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    void SetHeader(const std::wstring& title, const std::wstring& subtitle) { title_ = title; subtitle_ = subtitle; }

    void AddCard(D2D1_RECT_F rc, const std::wstring& title);
    void AddText(D2D1_RECT_F rc, const std::wstring& text);
    void AddCaption(D2D1_RECT_F rc, const std::wstring& text, bool right = false);
    void AddLink(D2D1_RECT_F rc, const std::wstring& text, const std::wstring& url, bool right = false);
    void AddItem(D2D1_RECT_F rc, const std::wstring& text, int id);
    void AddSlider(D2D1_RECT_F rc, const std::wstring& label, int lo, int hi, const std::wstring& unit,
                   std::function<int()> get, std::function<void(int)> set);
    void AddToggle(D2D1_RECT_F rc, const std::wstring& label, std::function<int()> get, std::function<void(int)> set);
    void AddButton(D2D1_RECT_F rc, const std::wstring& text, int id, bool primary = false);
    void AddSegment(D2D1_RECT_F rc, std::vector<std::wstring> items, std::function<int()> get,
                    std::function<void(int)> set);

    std::function<void()> onDestroy;

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT, WPARAM, LPARAM);
    bool CreateResources();
    void BuildHills();
    void Fit();
    void Paint();
    void Tick();
    void DrawBackground();
    void DrawWidget(const Widget& w, bool hot);
    void DrawFlakeIcon(float cx, float cy, float r, D2D1_COLOR_F color, float width);
    void Text(const std::wstring& s, IDWriteTextFormat* f, D2D1_RECT_F rc, D2D1_COLOR_F c);
    D2D1_RECT_F SliderTrack(const Widget& w) const;
    void SetSliderFromX(Widget& w, float x);
    int HitTest(float x, float y) const;

    HWND hwnd_ = nullptr;
    Handler onEvent_;
    bool popup_ = false;
    POINT anchor_ = {};
    float w_ = 0, h_ = 0, scale_ = 1;
    std::wstring title_, subtitle_;
    std::vector<Widget> widgets_;
    int hot_ = -1, drag_ = -1, pressed_ = -1;
    Palette pal_;

    std::vector<PanelFlake> flakes_;
    LARGE_INTEGER last_{}, freq_{};
    float time_ = 0;

    ComPtr<ID2D1Factory> d2d_;
    ComPtr<IDWriteFactory> dw_;
    ComPtr<ID2D1HwndRenderTarget> rt_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<ID2D1StrokeStyle> round_;
    ComPtr<ID2D1PathGeometry> hillBack_, hillFront_;
    ComPtr<IDWriteTextFormat> fTitle_, fSub_, fCard_, fLabel_, fValue_, fButton_, fCaption_, fSeg_;
};
