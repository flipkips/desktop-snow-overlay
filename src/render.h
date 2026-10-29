#pragma once
#include "common.h"
#include "snow.h"
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>

// Draws snow into a transparent window with Direct2D + DirectComposition (on the GPU)
class Renderer {
public:
    bool Init(HWND hwnd, int width, int height, float scale);
    void Shutdown();
    bool Ready() const { return dc_ != nullptr; }
    // false: device lost, call Init again
    bool Draw(const Snow& snow, const World& world);

private:
    bool CreateSprites();
    void DrawPiles(const Snow& snow);
    void DrawFlakes(const Snow& snow, const World& world, bool behind);

    ComPtr<ID3D11Device> d3d_;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<ID2D1Factory1> factory_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> dc_;
    ComPtr<ID2D1Bitmap1> target_;
    ComPtr<IDCompositionDevice> dcomp_;
    ComPtr<IDCompositionTarget> dcompTarget_;
    ComPtr<IDCompositionVisual> visual_;

    ComPtr<ID2D1Bitmap1> dot_, crystals_[kCrystalVariants];
    ComPtr<ID2D1SolidColorBrush> outline_;
    ComPtr<ID2D1LinearGradientBrush> fill_;
    float scale_ = 1.0f;
};
