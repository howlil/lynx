// M4-C candidate A: isolated Win32 Windows.UI.Composition geometry probe.
// NOT a replacement for LYNX's full layered GDI+ shell. The probe has no text,
// media, UIA or tray. Its only jobs are native path morph, transparent host,
// shape-constrained hit testing and reproducible timing/failure evidence.
//
// Controls: Ctrl+Alt+I expands/collapses; Ctrl+Alt+P switches placement;
//           Ctrl+Alt+Q quits; left-click shape expands, right-click changes mode.
//           During a compositor-driven morph, input is intentionally disabled.
//
// Official references:
// https://learn.microsoft.com/windows/uwp/composition/using-the-visual-layer-with-win32
// https://github.com/microsoft/Windows.UI.Composition-Win32-Samples/tree/master/cpp/HelloVectors

#include "../core/island_core.hpp"
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <DispatcherQueue.h>
#include <windows.ui.composition.interop.h>
#include <windows.graphics.interop.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
using namespace winrt;
using namespace Windows::UI::Composition;
using namespace Windows::UI::Composition::Desktop;
using namespace Windows::Foundation::Numerics;

constexpr int kFrameW=440;
constexpr int kFrameH=245;
constexpr UINT_PTR kMorphTimer=200;
constexpr int kExpandHotkey=201,kPlacementHotkey=202,kQuitHotkey=203;
constexpr DWORD kDurationMs=350;

struct GeoSource:implements<GeoSource,Windows::Graphics::IGeometrySource2D,
                                   ABI::Windows::Graphics::IGeometrySource2DInterop> {
    explicit GeoSource(com_ptr<ID2D1Geometry> path):path_(std::move(path)){}
    HRESULT STDMETHODCALLTYPE GetGeometry(ID2D1Geometry** out) override {
        if(!out)return E_POINTER;
        path_.copy_to(out);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TryGetGeometryUsingFactory(ID2D1Factory*,
                                                         ID2D1Geometry** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        return E_NOTIMPL;
    }
private:
    com_ptr<ID2D1Geometry> path_;
};

struct Measurement {
    LARGE_INTEGER frequency{};
    std::vector<double> samples;
    std::uint64_t failures=0;
    Measurement(){QueryPerformanceFrequency(&frequency);}
    template<typename F> void record(F&& work) {
        LARGE_INTEGER start{},end{};
        QueryPerformanceCounter(&start);
        try { work(); }
        catch(...) { ++failures; throw; }
        QueryPerformanceCounter(&end);
        if(frequency.QuadPart>0) {
            samples.push_back((end.QuadPart-start.QuadPart)*1000.0/
                              static_cast<double>(frequency.QuadPart));
        }
    }
    void exportCsv() const {
        wchar_t tmp[MAX_PATH]{};
        const auto n=GetTempPathW(MAX_PATH,tmp);
        if(!n || n>=MAX_PATH)return;
        const auto name=std::wstring(tmp)+L"lynx-composition-metrics.csv";
        FILE* fp=nullptr;
        if(_wfopen_s(&fp,name.c_str(),L"w")!=0 || !fp)return;
        auto sorted=samples;
        std::sort(sorted.begin(),sorted.end());
        double sum=0;
        for(auto v:samples)sum+=v;
        const double p95=sorted.empty()?0:sorted[
            static_cast<std::size_t>(std::ceil(sorted.size()*0.95)-1)];
        const double maximum=sorted.empty()?0:sorted.back();
        std::fprintf(fp,"renderer,total_draw_calls,failed_draw_calls,mean_cpu_ms,p95_recent_cpu_ms,max_cpu_ms,recent_window\n");
        std::fprintf(fp,"winui_composition_shape_probe,%llu,%llu,%.5f,%.5f,%.5f,%llu\n",
            static_cast<unsigned long long>(samples.size()),
            static_cast<unsigned long long>(failures),
            samples.empty()?0:sum/samples.size(),p95,maximum,
            static_cast<unsigned long long>(samples.size()));
        std::fclose(fp);
    }
};

struct State {
    HWND hwnd=nullptr;
    HMONITOR monitor=nullptr;
    float dpiScale=1.f;
    bool attached=false,expanded=false,animating=false;
    lynx::Geometry current{};
    winrt::com_ptr<ID2D1Factory> factory;
    winrt::com_ptr<ID2D1PathGeometry> hitGeometry;
    Windows::System::DispatcherQueueController queue{nullptr};
    Compositor compositor{nullptr};
    DesktopWindowTarget target{nullptr};
    ShapeVisual visual{nullptr};
    CompositionPathGeometry shapeGeometry{nullptr};
    CompositionPath currentPath{nullptr};
    CompositionPath nextPath{nullptr};
    winrt::com_ptr<ID2D1PathGeometry> nextHit;
    Measurement timing;
} g;

void addCubic(ID2D1GeometrySink* sink,D2D1_POINT_2F a,D2D1_POINT_2F b,
              D2D1_POINT_2F end) {
    sink->AddBezier(D2D1_BEZIER_SEGMENT{a,b,end});
}

winrt::com_ptr<ID2D1PathGeometry> buildGeometry(lynx::Geometry geo,bool attached) {
    winrt::com_ptr<ID2D1PathGeometry> result;
    check_hresult(g.factory->CreatePathGeometry(result.put()));
    winrt::com_ptr<ID2D1GeometrySink> sink;
    check_hresult(result->Open(sink.put()));
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    const float x=(kFrameW-geo.width)*.5f;
    const float y=geo.topOffset;
    const float right=x+geo.width;
    const float bottom=y+geo.height;
    const float k=.55228474983f;
    if(!attached) {
        const float r=std::min(geo.height*.5f,22.f);
        sink->BeginFigure({x+r,y},D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine({right-r,y});
        addCubic(sink.get(),{right-r+k*r,y},{right,y+r-k*r},{right,y+r});
        sink->AddLine({right,bottom-r});
        addCubic(sink.get(),{right,bottom-r+k*r},{right-r+k*r,bottom},{right-r,bottom});
        sink->AddLine({x+r,bottom});
        addCubic(sink.get(),{x+r-k*r,bottom},{x,bottom-r+k*r},{x,bottom-r});
        sink->AddLine({x,y+r});
        addCubic(sink.get(),{x,y+r-k*r},{x+r-k*r,y},{x+r,y});
    } else {
        const float shoulder=std::min(geo.shoulder,geo.height*.36f);
        const float leftBody=x+shoulder,rightBody=right-shoulder;
        const float r=std::min(22.f,std::max(4.f,(geo.height-shoulder)*.8f));
        sink->BeginFigure({x,y},D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine({right,y});
        addCubic(sink.get(),{right-shoulder*.85f,y},
            {rightBody,y+shoulder*.08f},{rightBody,y+shoulder});
        sink->AddLine({rightBody,bottom-r});
        addCubic(sink.get(),{rightBody,bottom-r+k*r},
            {rightBody-r+k*r,bottom},{rightBody-r,bottom});
        sink->AddLine({leftBody+r,bottom});
        addCubic(sink.get(),{leftBody+r-k*r,bottom},
            {leftBody,bottom-r+k*r},{leftBody,bottom-r});
        sink->AddLine({leftBody,y+shoulder});
        addCubic(sink.get(),{leftBody,y+shoulder*.08f},
            {x+shoulder*.85f,y},{x,y});
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    check_hresult(sink->Close());
    return result;
}

CompositionPath makePath(winrt::com_ptr<ID2D1PathGeometry> const& geometry) {
    return CompositionPath(winrt::make<GeoSource>(
        geometry.as<ID2D1Geometry>()));
}

void applyDpiAndPlacement() {
    const auto monitor=MonitorFromWindow(g.hwnd,MONITOR_DEFAULTTOPRIMARY);
    g.monitor=monitor;
    const UINT dpi=GetDpiForWindow(g.hwnd);
    g.dpiScale=(dpi?dpi:96)/96.f;
    // The composition visual operates in DIP coordinates; the HWND is sized
    // to the corresponding physical pixel box. No per-frame CPU pixels.
    g.visual.Scale({g.dpiScale,g.dpiScale,1.f});
    MONITORINFO info{sizeof(info)};
    if(GetMonitorInfoW(monitor,&info)) {
        const int w=static_cast<int>(std::ceil(kFrameW*g.dpiScale));
        const int h=static_cast<int>(std::ceil(kFrameH*g.dpiScale));
        const int x=info.rcMonitor.left+(info.rcMonitor.right-info.rcMonitor.left-w)/2;
        SetWindowPos(g.hwnd,HWND_TOPMOST,x,info.rcMonitor.top,w,h,
                     SWP_NOACTIVATE|SWP_SHOWWINDOW);
    }
}

void installStaticPath() {
    const auto geo=lynx::target(g.attached?lynx::Placement::Attached:
        lynx::Placement::Floating,g.expanded?lynx::View::Expanded:
        lynx::View::Ambient);
    g.current=geo;
    g.hitGeometry=buildGeometry(geo,g.attached);
    g.currentPath=makePath(g.hitGeometry);
    g.shapeGeometry.Path(g.currentPath);
}

void finishMorph() {
    KillTimer(g.hwnd,kMorphTimer);
    g.shapeGeometry.StopAnimation(L"Path");
    g.hitGeometry=std::move(g.nextHit);
    g.currentPath=g.nextPath;
    g.shapeGeometry.Path(g.currentPath);
    g.current=lynx::target(g.attached?lynx::Placement::Attached:
        lynx::Placement::Floating,g.expanded?lynx::View::Expanded:
        lynx::View::Ambient);
    g.animating=false;
}
void animateMorph() {
    if(g.animating)finishMorph(); // no queued stale animation completion
    g.expanded=!g.expanded;
    const auto desired=lynx::target(g.attached?lynx::Placement::Attached:
        lynx::Placement::Floating,g.expanded?lynx::View::Expanded:
        lynx::View::Ambient);
    g.timing.record([&] {
        g.nextHit=buildGeometry(desired,g.attached);
        g.nextPath=makePath(g.nextHit);
        const auto animation=g.compositor.CreatePathKeyFrameAnimation();
        animation.Duration(std::chrono::milliseconds(kDurationMs));
        animation.InsertKeyFrame(0.f,g.currentPath);
        animation.InsertKeyFrame(1.f,g.nextPath);
        g.shapeGeometry.StartAnimation(L"Path",animation);
    });
    g.animating=true;
    SetTimer(g.hwnd,kMorphTimer,kDurationMs+20,nullptr);
}

LRESULT CALLBACK wndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
        case WM_NCHITTEST: {
            if(g.animating || !g.hitGeometry)return HTTRANSPARENT;
            POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd,&pt);
            const D2D1_POINT_2F local{
                pt.x/g.dpiScale,pt.y/g.dpiScale
            };
            BOOL contains=FALSE;
            if(SUCCEEDED(g.hitGeometry->FillContainsPoint(local,nullptr,0.25f,&contains))
               && contains)return HTCLIENT;
            // This is not a guarantee of cross-process click-through.
            return HTTRANSPARENT;
        }
        case WM_LBUTTONUP:
            if(!g.animating)animateMorph();
            return 0;
        case WM_RBUTTONUP:
            if(!g.animating) {
                g.attached=!g.attached;
                installStaticPath();
            }
            return 0;
        case WM_HOTKEY:
            if(wp==kQuitHotkey) {DestroyWindow(hwnd);return 0;}
            if(g.animating)return 0;
            if(wp==kExpandHotkey)animateMorph();
            if(wp==kPlacementHotkey) {
                g.attached=!g.attached;installStaticPath();
            }
            return 0;
        case WM_TIMER:
            if(wp==kMorphTimer && g.animating)finishMorph();
            return 0;
        case WM_DPICHANGED:
            if(g.visual)applyDpiAndPlacement();
            return 0;
        case WM_DISPLAYCHANGE:
            if(g.visual)applyDpiAndPlacement();
            return 0;
        case WM_DESTROY:
            UnregisterHotKey(hwnd,kExpandHotkey);
            UnregisterHotKey(hwnd,kPlacementHotkey);
            UnregisterHotKey(hwnd,kQuitHotkey);
            KillTimer(hwnd,kMorphTimer);
            PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                        g.factory.put()));
        WNDCLASSW wc{};
        wc.hInstance=instance;
        wc.lpszClassName=L"LYNX_COMPOSITION_M4C_PROBE";
        wc.lpfnWndProc=wndProc;
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if(!RegisterClassW(&wc))winrt::throw_last_error();
        g.hwnd=CreateWindowExW(
            WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|
            WS_EX_NOREDIRECTIONBITMAP,
            wc.lpszClassName,L"LYNX Composition Candidate A",
            WS_POPUP,0,0,kFrameW,kFrameH,
            nullptr,nullptr,instance,nullptr);
        if(!g.hwnd)winrt::throw_last_error();

        DispatcherQueueOptions options{sizeof(DispatcherQueueOptions),
                                       DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};
        namespace abi=ABI::Windows::System;
        winrt::check_hresult(CreateDispatcherQueueController(options,
            reinterpret_cast<abi::IDispatcherQueueController**>(put_abi(g.queue))));
        g.compositor=Compositor();
        const auto interop=g.compositor.as<
            ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
        winrt::check_hresult(interop->CreateDesktopWindowTarget(g.hwnd,true,
            reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(
                put_abi(g.target))));
        g.visual=g.compositor.CreateShapeVisual();
        g.visual.Size({static_cast<float>(kFrameW),static_cast<float>(kFrameH)});
        g.shapeGeometry=g.compositor.CreatePathGeometry();
        auto shape=g.compositor.CreateSpriteShape(g.shapeGeometry);
        shape.FillBrush(g.compositor.CreateColorBrush(
            winrt::Windows::UI::Color{255,0,0,0}));
        g.visual.Shapes().Append(shape);
        g.target.Root(g.visual);
        installStaticPath();
        applyDpiAndPlacement();

        if(!RegisterHotKey(g.hwnd,kExpandHotkey,MOD_CONTROL|MOD_ALT,'I'))
            winrt::throw_last_error();
        if(!RegisterHotKey(g.hwnd,kPlacementHotkey,MOD_CONTROL|MOD_ALT,'P'))
            winrt::throw_last_error();
        if(!RegisterHotKey(g.hwnd,kQuitHotkey,MOD_CONTROL|MOD_ALT,'Q'))
            winrt::throw_last_error();
        ShowWindow(g.hwnd,SW_SHOWNOACTIVATE);
        MSG message{};
        while(GetMessageW(&message,nullptr,0,0)>0) {
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        g.timing.exportCsv();
        g.target=nullptr;
        g.visual=nullptr;
        g.shapeGeometry=nullptr;
        g.compositor=nullptr;
        g.hitGeometry=nullptr;
        g.factory=nullptr;
        if(g.queue) {
            auto shuttingDown=g.queue.ShutdownQueueAsync();
            // Current-thread dispatcher needs its Windows message pump to
            // finish closing; never block on async .get() on this thread.
            while(shuttingDown.Status()==winrt::Windows::Foundation::AsyncStatus::Started) {
                MSG pending{};
                while(PeekMessageW(&pending,nullptr,0,0,PM_REMOVE)) {
                    TranslateMessage(&pending);DispatchMessageW(&pending);
                }
                MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);
            }
            g.queue=nullptr;
        }
        return 0;
    } catch(const winrt::hresult_error& e) {
        wchar_t message[320]{};
        swprintf_s(message,L"Composition candidate could not initialize. HRESULT 0x%08X\n%s",
                   static_cast<unsigned>(e.code().value),e.message().c_str());
        MessageBoxW(nullptr,message,L"LYNX M4-C candidate A",MB_OK|MB_ICONERROR);
        g.timing.exportCsv();
        return 1;
    }
}
