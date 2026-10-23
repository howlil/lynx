#include "../core/activity_coordinator.hpp"
#include "media_provider.hpp"
#include "settings_store.hpp"

#include <windows.h>
#include <objbase.h>
#include <objidl.h>
#include <oleauto.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <UIAutomation.h>
#include <atomic>
#include <new>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cstdio>
#include <cstdint>
#include <string>
#include <memory>
#include <optional>
#include <utility>

using namespace Gdiplus;

namespace {
constexpr UINT_PTR kAnimationTimer = 101;
constexpr UINT_PTR kClockTimer = 102;
constexpr UINT_PTR kTransientTimer = 103;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kUiInvokeMessage = WM_APP + 2;
constexpr UINT kMediaMessage = WM_APP + 3;
constexpr UINT kTrayId = 1;
constexpr int kHotkeyId = 1;
constexpr float kCanvasWidthDip = 440.0f;
constexpr float kCanvasHeightDip = 260.0f;

enum MenuId : UINT {
    kShowHere = 2000, kFloating = 2001, kAttached, kAmbient, kMediaDemo,
    kTimerDemo, kTransientDemo, kReducedMotion, kFullscreenDemo, kToggleMedia, kQuit
};

struct App {
    HWND hwnd = nullptr;
    ULONG_PTR gdiplus = 0;
    lynx::Placement placement = lynx::Placement::Floating;
    lynx::ActivityCoordinator activities;
    lynx::Spring width{232}, height{42}, shoulder{0}, offset{13};
    bool reduced = false;
    std::uint64_t demoRevision = 0;
    std::uint64_t timerRevision = 0;
    std::uint64_t transientToken = 0;
    ULONGLONG transientDeadline = 0;
    bool hiddenByDemo = false;
    HWND priorFocus = nullptr;
    int dpi = 96;
    ULONGLONG previousFrame = 0;
    HMONITOR chosenMonitor = nullptr;
    bool firstFrame = true;
    IRawElementProviderSimple* uiaProvider = nullptr;
    std::unique_ptr<lynx::WindowsMediaProvider> mediaProvider;
    lynx::MediaSnapshot mediaSnapshot;
    std::wstring mediaSource;
    std::uint64_t mediaGeneration = 1;
    std::uint64_t mediaRevision = 0;
    bool mediaActive = false;
    SYSTEM_POWER_STATUS power{};
    bool powerKnown = false;
    HPOWERNOTIFY powerSourceSubscription = nullptr;
    HPOWERNOTIFY batterySubscription = nullptr;
};
App g;

// A single accessible shell button for the M1 surface. Future expanded
// controls require dedicated fragment providers; this does not pretend
// those controls exist before implementation.
class IslandUiaProvider final : public IRawElementProviderSimple, public IInvokeProvider {
public:
    explicit IslandUiaProvider(HWND hwnd) : hwnd_(hwnd) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IRawElementProviderSimple)) {
            *result = static_cast<IRawElementProviderSimple*>(this);
        } else if (iid == __uuidof(IInvokeProvider)) {
            *result = static_cast<IInvokeProvider*>(this);
        } else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* result) override {
        if (!result) return E_POINTER;
        *result = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern,
                                                 IUnknown** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (pattern == UIA_InvokePatternId) {
            *result = static_cast<IInvokeProvider*>(this);
            AddRef();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* value) override {
        if (!value) return E_POINTER;
        VariantInit(value);
        switch (id) {
            case UIA_ControlTypePropertyId:
                value->vt=VT_I4;
                value->lVal=UIA_ButtonControlTypeId;
                return S_OK;
            case UIA_NamePropertyId: {
                const auto view = g.activities.view();
                const auto selected = g.activities.primary();
                std::wstring name=L"LYNX Island";
                if (view==lynx::View::Expanded) name+=L", expanded, invoke to close";
                else if (view==lynx::View::Hidden) name+=L", hidden";
                else if (view==lynx::View::Transient) name+=L", "+g.activities.transientText();
                else if (selected) name+=L", "+selected->title+L", invoke to expand";
                else name+=L", ambient, invoke to expand";
                value->bstrVal=SysAllocString(name.c_str());
                if (!value->bstrVal) return E_OUTOFMEMORY;
                value->vt=VT_BSTR;
                return S_OK;
            }
            case UIA_AutomationIdPropertyId:
                value->bstrVal=SysAllocString(L"lynx.island.shell");
                if (!value->bstrVal) return E_OUTOFMEMORY;
                value->vt=VT_BSTR;
                return S_OK;
            case UIA_IsControlElementPropertyId:
            case UIA_IsContentElementPropertyId:
            case UIA_IsEnabledPropertyId:
                value->vt=VT_BOOL;
                value->boolVal=g.activities.view()==lynx::View::Hidden
                    ? VARIANT_FALSE : VARIANT_TRUE;
                return S_OK;
            case UIA_IsKeyboardFocusablePropertyId:
                value->vt=VT_BOOL;
                value->boolVal=g.activities.view()==lynx::View::Expanded
                    ? VARIANT_TRUE : VARIANT_FALSE;
                return S_OK;
            default: return S_OK;
        }
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
        IRawElementProviderSimple** result) override {
        if (!result) return E_POINTER;
        return UiaHostProviderFromHwnd(hwnd_,result);
    }
    HRESULT STDMETHODCALLTYPE Invoke() override {
        if (!IsWindow(hwnd_) || g.activities.view()==lynx::View::Hidden) return E_FAIL;
        return PostMessageW(hwnd_,kUiInvokeMessage,0,0) ? S_OK :
            HRESULT_FROM_WIN32(GetLastError());
    }
private:
    ~IslandUiaProvider() = default;
    std::atomic<ULONG> references_{1};
    HWND hwnd_ = nullptr;
};

void diagnostic(const char* operation, DWORD error = 0,
                int widthPx = 0, int heightPx = 0, unsigned alpha = 0) {
    char temp[MAX_PATH] = {};
    const DWORD n = GetTempPathA(MAX_PATH, temp);
    if (n == 0 || n >= MAX_PATH) return;
    const std::string path = std::string(temp) + "lynx-m0.log";
    HANDLE file = CreateFileA(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char line[512] = {};
    const int count = sprintf_s(line,
        "%04u-%02u-%02u %02u:%02u:%02u.%03u %-30s error=%lu hwnd=%p pixels=%dx%d center-alpha=%u visible=%d dpi=%d\r\n",
        now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,
        operation,static_cast<unsigned long>(error),static_cast<void*>(g.hwnd),widthPx,heightPx,
        alpha,g.hwnd && IsWindowVisible(g.hwnd) ? 1 : 0,g.dpi);
    if (count > 0) {
        DWORD written=0;
        WriteFile(file,line,static_cast<DWORD>(count),&written,nullptr);
        OutputDebugStringA(line);
    }
    CloseHandle(file);
}

float scale() { return static_cast<float>(g.dpi) / 96.0f; }

void readPower() {
    SYSTEM_POWER_STATUS current{};
    if (GetSystemPowerStatus(&current)) {
        g.power=current;
        g.powerKnown=true;
    } else {
        g.powerKnown=false;
    }
}

// This reducer runs only on the HWND UI thread. The provider's background MTA
// posts owned snapshots; it never calls ActivityCoordinator or GDI+ directly.
void onMediaSnapshot(lynx::MediaSnapshot next) {
    if (!next.available) {
        if (g.mediaActive) {
            g.activities.end(100,g.mediaGeneration,++g.mediaRevision);
            g.mediaActive=false;
        }
        g.mediaSnapshot=std::move(next);
        return;
    }
    if (g.mediaActive && next.source != g.mediaSource) {
        g.activities.end(100,g.mediaGeneration,++g.mediaRevision);
        ++g.mediaGeneration;
        g.mediaRevision=0;
        g.mediaActive=false;
    }
    g.mediaSource=next.source;
    g.mediaSnapshot=std::move(next);
    const std::wstring name = g.mediaSnapshot.title.empty() ?
        L"Now Playing" : g.mediaSnapshot.title;
    const std::wstring detail = g.mediaSnapshot.artist.empty() ?
        (g.mediaSnapshot.playing ? L"Playing" : L"Paused") :
        (g.mediaSnapshot.artist +
         (g.mediaSnapshot.playing ? L" · Playing" : L" · Paused"));
    if (g.activities.upsert(lynx::Activity{100,g.mediaGeneration,++g.mediaRevision,30,
            name,detail,true})) g.mediaActive=true;
}


void timerAtMinuteBoundary() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const UINT millis = std::max<UINT>(250, 60000u - now.wSecond * 1000u - now.wMilliseconds);
    SetTimer(g.hwnd, kClockTimer, millis, nullptr);
}

void setTargets() {
    const auto geo = lynx::target(g.placement, g.activities.view());
    if (g.reduced) {
        g.width.snap(geo.width);
        g.height.snap(geo.height);
        g.shoulder.snap(geo.shoulder);
        g.offset.snap(geo.topOffset);
    } else {
        g.width.retarget(geo.width);
        g.height.retarget(geo.height);
        g.shoulder.retarget(geo.shoulder);
        g.offset.retarget(geo.topOffset);
    }
    if (g.activities.view() == lynx::View::Hidden) {
        KillTimer(g.hwnd, kAnimationTimer);
        ShowWindow(g.hwnd, SW_HIDE);
        return;
    }
    ShowWindow(g.hwnd, SW_SHOWNOACTIVATE);
    if (g.width.moving() || g.height.moving() || g.shoulder.moving() || g.offset.moving()) {
        g.previousFrame = GetTickCount64();
        SetTimer(g.hwnd, kAnimationTimer, 16, nullptr);
    } else KillTimer(g.hwnd, kAnimationTimer);
}

void rounded(GraphicsPath& path, float x, float y, float w, float h, float r) {
    r = std::clamp(r, 1.0f, std::min(w, h) * 0.5f);
    const float d = 2.0f * r;
    path.StartFigure();
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.AddArc(x, y, d, d, 180, 90);
    path.CloseFigure();
}

// Body and concave shoulders form one filled contour. No rectangular hitbox.
void attachedPath(GraphicsPath& path, float x, float y, float w, float h, float shoulder) {
    const float s = std::min(shoulder, std::min(w * 0.15f, h * 0.36f));
    const float radius = std::min(22.0f, std::max(4.0f, (h - s) * 0.80f));
    const float k = 0.55228475f;
    const float left = x + s, right = x + w - s, bottom = y + h;
    path.StartFigure();
    path.AddBezier(PointF(x, y), PointF(x+s*.85f,y),
                   PointF(left,y+s*.08f), PointF(left,y+s));
    path.AddLine(left,y+s,left,bottom-radius);
    path.AddBezier(PointF(left,bottom-radius),PointF(left,bottom-radius+k*radius),
                   PointF(left+radius-k*radius,bottom),PointF(left+radius,bottom));
    path.AddLine(left+radius,bottom,right-radius,bottom);
    path.AddBezier(PointF(right-radius,bottom),PointF(right-radius+k*radius,bottom),
                   PointF(right,bottom-radius+k*radius),PointF(right,bottom-radius));
    path.AddLine(right,bottom-radius,right,y+s);
    path.AddBezier(PointF(right,y+s),PointF(right,y+s*.08f),
                   PointF(x+w-s*.85f,y),PointF(x+w,y));
    path.AddLine(x+w,y,x,y);
    path.CloseFigure();
}

void label(Graphics& graphics, const std::wstring& text, float px, float x, float y,
           Color color, bool bold = false) {
    Font font(L"Segoe UI", px, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush brush(color);
    graphics.DrawString(text.c_str(), -1, &font, PointF(x,y), &brush);
}

bool drawFrame() {
    if (g.activities.view() == lynx::View::Hidden) return true;
    const float dpi = scale();
    // Tight HWND: no invisible 440x260 rectangle covering desktop interaction.
    const float canvasWidthDip = std::ceil(g.width.value()) + 4.0f;
    const float canvasHeightDip = std::ceil(g.offset.value() + g.height.value()) + 3.0f;
    const int W = static_cast<int>(std::ceil(canvasWidthDip * dpi));
    const int H = static_cast<int>(std::ceil(canvasHeightDip * dpi));
    if (W <= 0 || H <= 0) { diagnostic("invalid surface dimensions"); return false; }

    HDC screen = GetDC(nullptr);
    if (!screen) { diagnostic("GetDC failed", GetLastError()); return false; }
    HDC memory = CreateCompatibleDC(screen);
    if (!memory) {
        diagnostic("CreateCompatibleDC failed",GetLastError());
        ReleaseDC(nullptr,screen);
        return false;
    }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;  // top-down BGRA buffer
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dib || !pixels) {
        if (dib) DeleteObject(dib);
        diagnostic("CreateDIBSection failed",GetLastError(),W,H);
        DeleteDC(memory);
        ReleaseDC(nullptr,screen);
        return false;
    }
    const HGDIOBJ old = SelectObject(memory, dib);

    {
        Bitmap surface(W, H, W * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(pixels));
        Graphics graphics(&surface);
        graphics.Clear(Color(0,0,0,0));
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetCompositingQuality(CompositingQualityHighQuality);
        graphics.ScaleTransform(dpi,dpi);

        const float w = g.width.value(), h = g.height.value();
        if (w > 10.0f && h > 5.0f) {
            const float x = 2.0f;
            const float y = g.offset.value();
            const float shoulder = g.shoulder.value();
            const bool attached = g.placement == lynx::Placement::Attached;
            GraphicsPath shape;
            if (attached && shoulder > 0.2f) attachedPath(shape,x,y,w,h,shoulder);
            else rounded(shape,x,y,w,h,std::min(h*.5f,22.0f));
            SolidBrush background(Color(255,0,0,0));
            graphics.FillPath(&background,&shape);

            const float contentLeft = x + shoulder + 17.0f;
            const float contentRight = x + w - shoulder - 16.0f;
            const auto currentView = g.activities.view();

            if (currentView == lynx::View::Ambient || currentView == lynx::View::Expanded) {
                SYSTEMTIME now{};
                GetLocalTime(&now);
                wchar_t clockText[12]{};
                swprintf_s(clockText, L"%02u:%02u", now.wHour, now.wMinute);
                label(graphics,clockText,14.0f,contentLeft,y+10.0f,Color(255,245,245,247),true);

                const float center = x+w*0.5f;
                SolidBrush lens(Color(255,17,17,20));
                Pen lensStroke(Color(255,30,30,35),0.8f);
                graphics.FillEllipse(&lens,center-17.0f,y+16.0f,34.0f,10.0f);
                graphics.DrawEllipse(&lensStroke,center-17.0f,y+16.0f,34.0f,10.0f);

                const auto& power = g.power;
                const bool read = g.powerKnown;
                std::wstring powerText = L"";
                if (read && power.BatteryFlag != 128 &&
                    power.BatteryFlag != 255 && power.BatteryLifePercent != 255) {
                    powerText = std::to_wstring(static_cast<int>(power.BatteryLifePercent)) + L"%";
                } else if (read && power.ACLineStatus == 1) {
                    powerText = L"AC";
                }
                if (!powerText.empty()) {
                    const float textX = contentRight - (powerText.size()>2 ? 51.0f : 38.0f);
                    const bool charging = read && power.ACLineStatus == 1 && power.BatteryFlag != 128;
                    const Color accent = charging ? Color(255,48,209,88) : Color(255,245,245,247);
                    label(graphics,powerText,12,textX,y+12,accent,true);
                    Pen batteryPen(accent,1.5f);
                    graphics.DrawRectangle(&batteryPen,contentRight-18.0f,y+15.0f,15.0f,10.0f);
                    graphics.DrawLine(&batteryPen,contentRight-1,y+18,contentRight-1,y+22);
                    if (read && power.BatteryLifePercent != 255 && power.BatteryFlag != 128) {
                        SolidBrush chargeFill(accent);
                        graphics.FillRectangle(&chargeFill,contentRight-16,y+17,
                            11.0f*(power.BatteryLifePercent/100.0f),6.0f);
                    }
                }
                if (currentView == lynx::View::Expanded) {
                    const auto selected = g.activities.primary();
                    Pen divider(Color(255,40,40,45),1.0f);
                    graphics.DrawLine(&divider,x+20,y+48,x+w-20,y+48);
                    label(graphics,selected ? selected->title : L"LYNX  /  NATIVE SHELL",13,x+23,y+64,
                          Color(255,245,245,247),true);
                    label(graphics,selected ? selected->subtitle : L"Ambient clock and power are live Windows data.",12,
                          x+23,y+94,Color(255,164,164,170));
                    label(graphics,L"Click to collapse  |  Right-click for scenarios",11,
                          x+23,y+120,Color(255,140,140,148));
                    label(graphics,L"Renderer: layered alpha spike; compositor undecided",10,
                          x+23,y+153,Color(255,115,115,122));
                }
            } else if (currentView == lynx::View::Compact) {
                SolidBrush cover(Color(255,68,97,165));
                graphics.FillEllipse(&cover,x+shoulder+10.0f,y+8.0f,26.0f,26.0f);
                const auto selected = g.activities.primary();
                label(graphics,selected ? selected->title : L"Activity",12,
                      x+shoulder+46,y+10,Color(255,245,245,247),true);
                if (g.activities.secondary()) {
                    SolidBrush indicator(Color(255,10,132,255));
                    graphics.FillEllipse(&indicator,contentRight-6.0f,y+17.0f,6.0f,6.0f);
                }
            } else if (currentView == lynx::View::Transient) {
                label(graphics,g.activities.transientText(),13,contentLeft,y+15,
                      Color(255,48,209,88),true);
            }
        }
    }

    // The center of the black capsule must be opaque. An empty alpha surface
    // leaves a running process with no visible UI.
    const int sampleX = std::clamp(W / 2, 0, W - 1);
    const int sampleY = std::clamp(static_cast<int>((g.offset.value() + g.height.value() * .5f) * dpi), 0, H - 1);
    const unsigned centerAlpha = (static_cast<std::uint32_t*>(pixels)[sampleY * W + sampleX] >> 24) & 0xffu;
    if (centerAlpha == 0) {
        diagnostic("blank alpha buffer",0,W,H,centerAlpha);
        SelectObject(memory,old); DeleteObject(dib); DeleteDC(memory); ReleaseDC(nullptr,screen);
        return false;
    }
    MONITORINFO monitor{sizeof(MONITORINFO)};
    HMONITOR display = g.chosenMonitor ? g.chosenMonitor :
        MonitorFromWindow(g.hwnd, MONITOR_DEFAULTTOPRIMARY);
    if (!GetMonitorInfoW(display,&monitor)) {
        monitor.rcMonitor = RECT{0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
    }
    POINT dst{monitor.rcMonitor.left +
                  ((monitor.rcMonitor.right-monitor.rcMonitor.left)-W)/2,
              monitor.rcMonitor.top};
    POINT src{0,0};
    SIZE size{W,H};
    BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    const BOOL updated = UpdateLayeredWindow(g.hwnd, screen, &dst, &size,
                                             memory, &src, 0, &blend, ULW_ALPHA);
    const DWORD updateError = updated ? 0 : GetLastError();
    if (!updated || g.firstFrame) diagnostic(updated ? "first layered frame OK" :
        "UpdateLayeredWindow failed",updateError,W,H,centerAlpha);
    if (updated) g.firstFrame = false;

    SelectObject(memory,old);
    DeleteObject(dib);
    DeleteDC(memory);
    ReleaseDC(nullptr,screen);
    return updated != FALSE;
}

void refresh() { setTargets(); drawFrame(); }

void setExpanded(bool wantExpanded) {
    if (wantExpanded) {
        g.priorFocus = GetForegroundWindow();
        g.activities.expand();
        if (g.activities.view() != lynx::View::Expanded) return;
        // Intentional expansion may become keyboard-active; passive compact does not.
        auto style = GetWindowLongPtrW(g.hwnd,GWL_EXSTYLE);
        SetWindowLongPtrW(g.hwnd,GWL_EXSTYLE,style & ~WS_EX_NOACTIVATE);
        refresh();
        SetForegroundWindow(g.hwnd);
        SetFocus(g.hwnd);
    } else {
        g.activities.collapse();
        const auto style = GetWindowLongPtrW(g.hwnd,GWL_EXSTYLE);
        SetWindowLongPtrW(g.hwnd,GWL_EXSTYLE,style | WS_EX_NOACTIVATE);
        refresh();
        if (g.priorFocus && IsWindow(g.priorFocus)) SetForegroundWindow(g.priorFocus);
        g.priorFocus = nullptr;
    }
}

void toggleExpanded() { setExpanded(g.activities.view() != lynx::View::Expanded); }

void menu(HWND hwnd) {
    HMENU popup = CreatePopupMenu();
    if (!popup) return;
    AppendMenuW(popup, MF_STRING, kShowHere, L"Show Island on this monitor");
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_STRING | (g.placement==lynx::Placement::Floating?MF_CHECKED:0),
                kFloating,L"Floating");
    AppendMenuW(popup, MF_STRING | (g.placement==lynx::Placement::Attached?MF_CHECKED:0),
                kAttached,L"Top-attached");
    AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
    AppendMenuW(popup,MF_STRING,kAmbient,L"Clear simulated activities");
    AppendMenuW(popup,MF_STRING,kMediaDemo,L"Start / update simulated media");
    AppendMenuW(popup,MF_STRING,kTimerDemo,L"Start / update simulated focus timer");
    AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
    AppendMenuW(popup,MF_STRING |
                ((g.mediaSnapshot.available && g.mediaSnapshot.canToggle)?MF_ENABLED:MF_GRAYED),
                kToggleMedia,g.mediaSnapshot.playing?
                  L"Pause current Windows media":L"Play current Windows media");
    AppendMenuW(popup,MF_STRING,kTransientDemo,L"Simulate transient charging");
    AppendMenuW(popup,MF_STRING | (g.reduced?MF_CHECKED:0),
                kReducedMotion,L"Reduced motion");
    AppendMenuW(popup,MF_STRING,kFullscreenDemo,
                g.hiddenByDemo?L"Restore from hidden":L"Simulate fullscreen hide");
    AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
    AppendMenuW(popup,MF_STRING,kQuit,L"Quit LYNX M0");
    HWND beforeMenuFocus = GetForegroundWindow();
    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    const UINT command=TrackPopupMenu(popup,TPM_RETURNCMD|TPM_RIGHTBUTTON,
                                       pt.x,pt.y,0,hwnd,nullptr);
    DestroyMenu(popup);
    PostMessageW(hwnd,WM_NULL,0,0);
    switch (command) {
        case kShowHere:
            if (g.activities.fullscreen()) g.activities.setFullscreen(false);
            g.hiddenByDemo = false;
            g.chosenMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
            diagnostic("show-on-this-monitor");
            break;
        case kFloating:
            g.placement=lynx::Placement::Floating;
            if (!lynx::WindowsSettingsStore::savePlacement(g.placement))
                diagnostic("save placement failed",GetLastError());
            break;
        case kAttached:
            g.placement=lynx::Placement::Attached;
            if (!lynx::WindowsSettingsStore::savePlacement(g.placement))
                diagnostic("save placement failed",GetLastError());
            break;
        case kAmbient:
            g.activities.collapse();
            g.activities.end(1,1,++g.demoRevision);
            g.activities.end(2,1,++g.timerRevision);
            break;
        case kMediaDemo:
            g.activities.upsert(lynx::Activity{1,1,++g.demoRevision,10,
                  L"Now playing",L"Demo activity source",true});
            break;
        case kTimerDemo:
            g.activities.upsert(lynx::Activity{2,1,++g.timerRevision,20,
                  L"Focus timer",L"25:00  /  Demo in progress",true});
            break;
        case kTransientDemo:
            g.transientToken=g.activities.transient(L"Power connected");
            if (g.transientToken) {
                g.transientDeadline=GetTickCount64()+2800;
                SetTimer(hwnd,kTransientTimer,2800,nullptr);
            }
            break;
        case kToggleMedia:
            if (g.mediaProvider && g.mediaSnapshot.available && g.mediaSnapshot.canToggle)
                g.mediaProvider->togglePlayPause();
            break;
        case kReducedMotion:
            g.reduced=!g.reduced;
            if (!lynx::WindowsSettingsStore::saveReducedMotion(g.reduced))
                diagnostic("save motion preference failed",GetLastError());
            break;
        case kFullscreenDemo:
            g.hiddenByDemo=!g.hiddenByDemo;
            g.activities.setFullscreen(g.hiddenByDemo);
            break;
        case kQuit: DestroyWindow(hwnd); return;
        default: return;
    }
    if (g.activities.view() != lynx::View::Expanded) {
        const auto style=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);
        SetWindowLongPtrW(hwnd,GWL_EXSTYLE,style | WS_EX_NOACTIVATE);
        HWND restore=g.priorFocus ? g.priorFocus : beforeMenuFocus;
        if (restore && restore!=hwnd && IsWindow(restore)) SetForegroundWindow(restore);
        g.priorFocus=nullptr;
    }
    refresh();
}

void addTray(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize=sizeof(nid); nid.hWnd=hwnd; nid.uID=kTrayId;
    nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;
    nid.uCallbackMessage=kTrayMessage;
    nid.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
    wcscpy_s(nid.szTip,L"LYNX M0 · Ctrl+Alt+Space");
    Shell_NotifyIconW(NIM_ADD,&nid);
}
void removeTray(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize=sizeof(nid); nid.hWnd=hwnd; nid.uID=kTrayId;
    Shell_NotifyIconW(NIM_DELETE,&nid);
}

LRESULT CALLBACK wndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
        case WM_CREATE: {
            g.hwnd=hwnd;
            g.dpi=static_cast<int>(GetDpiForWindow(hwnd));
            if (g.dpi<=0) g.dpi=96;
            BOOL animations=TRUE;
            if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0)) {
                g.reduced=!animations;
            }
            const auto preferences=lynx::WindowsSettingsStore::load();
            g.placement=preferences.placement;
            if (preferences.hasReducedMotionOverride)
                g.reduced=preferences.reducedMotion;
            // Prepare springs at the persisted placement before the first frame.
            const auto initialGeometry=lynx::target(g.placement,lynx::View::Ambient);
            g.width.snap(initialGeometry.width);
            g.height.snap(initialGeometry.height);
            g.shoulder.snap(initialGeometry.shoulder);
            g.offset.snap(initialGeometry.topOffset);
            readPower();
            g.powerSourceSubscription=RegisterPowerSettingNotification(
                hwnd,&GUID_ACDC_POWER_SOURCE,DEVICE_NOTIFY_WINDOW_HANDLE);
            g.batterySubscription=RegisterPowerSettingNotification(
                hwnd,&GUID_BATTERY_PERCENTAGE_REMAINING,DEVICE_NOTIFY_WINDOW_HANDLE);
            if (!g.powerSourceSubscription || !g.batterySubscription)
                diagnostic("power subscribe failed",GetLastError());
            RegisterHotKey(hwnd,kHotkeyId,MOD_CONTROL|MOD_ALT,VK_SPACE);
            addTray(hwnd);
            timerAtMinuteBoundary();
            diagnostic("WM_CREATE complete");
            return 0;
        }
        case WM_MOUSEACTIVATE:
            if (g.activities.view() != lynx::View::Expanded) return MA_NOACTIVATE;
            break;
        case WM_LBUTTONUP:
            toggleExpanded();
            return 0;
        case WM_RBUTTONUP:
            menu(hwnd);
            return 0;
        case WM_HOTKEY:
            if (wp==kHotkeyId) toggleExpanded();
            return 0;
        case kUiInvokeMessage:
            toggleExpanded();
            return 0;
        case kMediaMessage: {
            std::unique_ptr<lynx::MediaSnapshot> media(
                 reinterpret_cast<lynx::MediaSnapshot*>(lp));
            if (!media) return 0;
            const auto previousView=g.activities.view();
            const auto oldSource=g.mediaSource;
            const auto oldAvailable=g.mediaSnapshot.available;
            const auto oldPlaying=g.mediaSnapshot.playing;
            const auto oldTitle=g.mediaSnapshot.title;
            onMediaSnapshot(std::move(*media));
            if (previousView!=g.activities.view() ||
                oldSource!=g.mediaSource ||
                oldAvailable!=g.mediaSnapshot.available ||
                oldPlaying!=g.mediaSnapshot.playing ||
                oldTitle!=g.mediaSnapshot.title)
                refresh();
            return 0;
        }
        case WM_GETOBJECT:
            if (static_cast<LONG>(lp)==UiaRootObjectId) {
                if (!g.uiaProvider) g.uiaProvider=new (std::nothrow) IslandUiaProvider(hwnd);
                if (g.uiaProvider)
                    return UiaReturnRawElementProvider(hwnd,wp,lp,g.uiaProvider);
            }
            break;
        case WM_KEYDOWN:
            if (g.activities.view()==lynx::View::Expanded &&
                (wp==VK_ESCAPE || wp==VK_RETURN || wp==VK_SPACE)) {
                setExpanded(false);
                return 0;
            }
            break;
        case WM_TIMER:
            if (wp==kClockTimer) { timerAtMinuteBoundary(); drawFrame(); return 0; }
            if (wp==kTransientTimer) {
                const auto now=GetTickCount64();
                // Windows timer messages carry only their ID. A queued timeout
                // from an older notification must not prematurely expire a
                // newer one, even when the timer ID has been reused.
                if (g.transientDeadline>now) {
                    SetTimer(hwnd,kTransientTimer,
                        static_cast<UINT>(std::max<ULONGLONG>(
                            1,g.transientDeadline-now)),nullptr);
                    return 0;
                }
                KillTimer(hwnd,kTransientTimer);
                if (g.activities.expireTransient(g.transientToken)) refresh();
                return 0;
            }
            if (wp==kAnimationTimer) {
                const ULONGLONG now=GetTickCount64();
                const float seconds=static_cast<float>(now-g.previousFrame)/1000.0f;
                g.previousFrame=now;
                g.width.step(seconds); g.height.step(seconds);
                g.shoulder.step(seconds); g.offset.step(seconds);
                if (!g.width.moving() && !g.height.moving() &&
                    !g.shoulder.moving() && !g.offset.moving()) KillTimer(hwnd,kAnimationTimer);
                drawFrame();
                return 0;
            }
            break;
        case WM_TIMECHANGE:
            timerAtMinuteBoundary();
            drawFrame();
            return 0;
        case WM_POWERBROADCAST:
            if (wp==PBT_POWERSETTINGCHANGE || wp==PBT_APMPOWERSTATUSCHANGE ||
                wp==PBT_APMRESUMEAUTOMATIC || wp==PBT_APMRESUMESUSPEND) {
                // Cache hardware data once per change, never once per animation frame.
                const auto previous=g.power;
                const bool known=g.powerKnown;
                readPower();
                const bool changed = known!=g.powerKnown ||
                  (g.powerKnown && (!known ||
                    previous.ACLineStatus!=g.power.ACLineStatus ||
                    previous.BatteryLifePercent!=g.power.BatteryLifePercent ||
                    previous.BatteryFlag!=g.power.BatteryFlag));
                if (changed) {
                    const bool chargingChanged=known && g.powerKnown &&
                        previous.ACLineStatus != g.power.ACLineStatus;
                    if (chargingChanged && g.power.ACLineStatus == 1) {
                        g.transientToken=g.activities.transient(L"Power connected");
                        if (g.transientToken) {
                            g.transientDeadline=GetTickCount64()+2800;
                            SetTimer(hwnd,kTransientTimer,2800,nullptr);
                        }
                    }
                    refresh();
                }
                return TRUE;
            }
            break;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            g.dpi=static_cast<int>(GetDpiForWindow(hwnd));
            if (g.dpi<=0)g.dpi=96;
            drawFrame();
            return 0;
        case WM_SETTINGCHANGE:
            drawFrame(); return 0;
        case kTrayMessage:
            if (lp==WM_RBUTTONUP || lp==WM_CONTEXTMENU) menu(hwnd);
            else if (lp==WM_LBUTTONDBLCLK) toggleExpanded();
            return 0;
        case WM_DESTROY:
            g.mediaProvider.reset(); // join MTA before HWND is destroyed
            MSG unconsumed{};
            while (PeekMessageW(&unconsumed,hwnd,kMediaMessage,kMediaMessage,PM_REMOVE))
                delete reinterpret_cast<lynx::MediaSnapshot*>(unconsumed.lParam);
            if (g.powerSourceSubscription) {
                UnregisterPowerSettingNotification(g.powerSourceSubscription);
                g.powerSourceSubscription=nullptr;
            }
            if (g.batterySubscription) {
                UnregisterPowerSettingNotification(g.batterySubscription);
                g.batterySubscription=nullptr;
            }
            if (g.uiaProvider) {
                UiaReturnRawElementProvider(hwnd,0,0,nullptr);
                g.uiaProvider->Release();
                g.uiaProvider=nullptr;
            }
            KillTimer(hwnd,kAnimationTimer);
            KillTimer(hwnd,kClockTimer);
            KillTimer(hwnd,kTransientTimer);
            UnregisterHotKey(hwnd,kHotkeyId);
            removeTray(hwnd);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE singleton=CreateMutexW(nullptr,TRUE,L"Local\\LYNX_M0_SINGLE_INSTANCE");
    const DWORD instanceError = singleton ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
    if (!singleton || instanceError == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr,
            instanceError == ERROR_ALREADY_EXISTS
              ? L"LYNX M0 is already running.\\nClose the old instance from the system tray or Task Manager before launching this build."
              : L"LYNX M0 could not create its single-instance mutex.",
            L"LYNX M0",MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        if (singleton) CloseHandle(singleton);
        return instanceError == ERROR_ALREADY_EXISTS ? 0 : 5;
    }
    GdiplusStartupInput startup{};
    if (GdiplusStartup(&g.gdiplus,&startup,nullptr)!=Ok) {
        CloseHandle(singleton); return 1;
    }
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc=wndProc; wc.hInstance=instance;
    wc.lpszClassName=L"LYNX_M0_NATIVE_OVERLAY";
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if (!RegisterClassExW(&wc)) {
        GdiplusShutdown(g.gdiplus); CloseHandle(singleton); return 2;
    }
    HWND hwnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_NOACTIVATE,
         wc.lpszClassName,L"LYNX M0",WS_POPUP,0,0,440,260,
         nullptr,nullptr,instance,nullptr);
    if (!hwnd) {
        GdiplusShutdown(g.gdiplus); CloseHandle(singleton); return 3;
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    g.chosenMonitor=MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    // First supply fully initialized layered pixels before showing the window.
    if (!drawFrame()) {
        diagnostic("startup aborted");
        MessageBoxW(nullptr,
            L"LYNX cannot render its Island.\n\nSee %TEMP%\\lynx-m0.log for the Windows error code and alpha diagnostics.",
            L"LYNX M0 — Overlay initialization failed", MB_OK | MB_ICONERROR | MB_TOPMOST);
        DestroyWindow(hwnd);
        GdiplusShutdown(g.gdiplus);
        CloseHandle(singleton);
        return 4;
    }
    ShowWindow(hwnd,SW_SHOWNOACTIVATE);
    SetWindowPos(hwnd,HWND_TOPMOST,0,0,0,0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    diagnostic("startup overlay shown");
    g.mediaProvider=std::make_unique<lynx::WindowsMediaProvider>(
        [hwnd](lynx::MediaSnapshot snapshot) {
            auto message=std::make_unique<lynx::MediaSnapshot>(std::move(snapshot));
            if (PostMessageW(hwnd,kMediaMessage,0,
                    reinterpret_cast<LPARAM>(message.get())))
                (void)message.release(); // ownership transfers to UI message
        });
    MSG msg{};
    while(GetMessageW(&msg,nullptr,0,0)>0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    GdiplusShutdown(g.gdiplus);
    CloseHandle(singleton);
    return static_cast<int>(msg.wParam);
}
