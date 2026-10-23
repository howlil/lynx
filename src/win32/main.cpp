#include "../core/media_activity.hpp"
#include "../core/fullscreen_policy.hpp"
#include "../core/focus_timer.hpp"
#include "../core/file_shelf.hpp"
#include "volume_feedback.hpp"
#include "file_drop_target.hpp"
#include "media_provider.hpp"
#include "settings_store.hpp"
#include "render_metrics.hpp"
#include "uia_shell.hpp"

#include <windows.h>
#include <objbase.h>
#include <objidl.h>
#include <oleauto.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shellscalingapi.h>
#include <windowsx.h>
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
constexpr UINT_PTR kFocusTimer = 104;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kUiInvokeMessage = WM_APP + 2;
constexpr UINT kMediaMessage = WM_APP + 3;
constexpr UINT kVolumeMessage = WM_APP + 4;
constexpr UINT kForegroundMessage = WM_APP + 5;
constexpr UINT kUiActionMessage = WM_APP + 6;
constexpr UINT kUiFocusMessage = WM_APP + 7;
UINT gTaskbarRestarted = 0;
constexpr UINT kTrayId = 1;
constexpr int kHotkeyId = 1;
constexpr float kCanvasWidthDip = 440.0f;
constexpr float kCanvasHeightDip = 260.0f;

enum MenuId : UINT {
    kShowHere = 2000, kFloating = 2001, kAttached, kAmbient, kMediaDemo,
    kTimerDemo, kFocus5, kFocus25, kFocusPause, kFocusReset,
    kShelfOpen, kShelfClear, kTransientDemo, kReducedMotion, kFullscreenDemo, kToggleMedia, kQuit
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
    lynx::FocusTimer focus;
    lynx::FileShelf shelf;
    lynx::FileDropTarget* dropTarget = nullptr;
    bool dropRegistered = false;
    std::unique_ptr<lynx::WindowsVolumeFeedback> volume;
    lynx::WindowsVolumeFeedback::Status volumeState{};
    bool volumeInitialized = false;
    std::uint64_t transientToken = 0;
    ULONGLONG transientDeadline = 0;
    bool hiddenByDemo = false;
    bool actualFullscreen = false;
    HWINEVENTHOOK foregroundHook = nullptr;
    HWND priorFocus = nullptr;
    int dpi = 96;
    ULONGLONG previousFrame = 0;
    HMONITOR chosenMonitor = nullptr;
    HMONITOR dpiMonitor = nullptr;
    bool firstFrame = true;
    lynx::ShellUiaRoot* uiaProvider = nullptr;
    lynx::RenderMetrics renderMetrics;
    int focusedAction=-1;
    std::unique_ptr<lynx::WindowsMediaProvider> mediaProvider;
    lynx::MediaSnapshot mediaSnapshot;
    lynx::MediaActivityBridge mediaBridge;
    SYSTEM_POWER_STATUS power{};
    bool powerKnown = false;
    HPOWERNOTIFY powerSourceSubscription = nullptr;
    HPOWERNOTIFY batterySubscription = nullptr;
};
App g;

lynx::ContextActions currentActions() {
    const auto activity=g.activities.primary();
    return lynx::contextActions(g.activities.view(),activity?activity->id:0,
        g.focus.state(),
        g.mediaSnapshot.available && g.mediaSnapshot.canToggle,
        g.mediaSnapshot.playing,!g.shelf.files().empty());
}

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

void updateDpiForMonitor() {
    if(g.chosenMonitor == g.dpiMonitor)return;
    g.dpiMonitor=g.chosenMonitor;
    UINT x=96,y=96;
    if(g.chosenMonitor &&
       SUCCEEDED(GetDpiForMonitor(g.chosenMonitor,MDT_EFFECTIVE_DPI,&x,&y)) &&
       x>0) {
        g.dpi=static_cast<int>(x);
    } else {
        g.dpi=g.hwnd ? static_cast<int>(GetDpiForWindow(g.hwnd)) : 96;
        if(g.dpi<=0)g.dpi=96;
    }
}
float scale() { return static_cast<float>(g.dpi) / 96.0f; }

void syncAccessibleState() {
    if(!g.uiaProvider)return;
    lynx::AccessibleSnapshot state;
    state.visible=g.activities.view()!=lynx::View::Hidden && IsWindowVisible(g.hwnd);
    state.expanded=state.visible && g.activities.view()==lynx::View::Expanded;
    const auto selected=g.activities.primary();
    state.name=L"LYNX Island";
    if(state.expanded)state.name+=L", expanded";
    if(selected)state.name+=L", "+selected->title;
    if(g.activities.view()==lynx::View::Transient)
        state.name+=L", "+g.activities.transientText();
    GetWindowRect(g.hwnd,&state.screen);
    const auto items=currentActions();
    state.count=state.expanded?items.count:0;
    state.focused=g.focusedAction;
    const float ratio=scale();
    const float origin=2.0f;
    const float w=g.width.value();
    const float y=g.offset.value()+125.0f;
    for(int index=0;index<state.count;index++) {
        const auto action=items.at(index);
        if(!action)continue;
        const float left=origin+w-(state.count==1?85.0f:
                                          index==0?168.0f:88.0f);
        const float width=state.count==1?63.0f:index==0?72.0f:66.0f;
        const int x=state.screen.left+static_cast<int>(std::lround(left*ratio));
        const int yy=state.screen.top+static_cast<int>(std::lround(y*ratio));
        const int xx=state.screen.left+static_cast<int>(std::lround((left+width)*ratio));
        const int bottom=state.screen.top+static_cast<int>(std::lround((y+31.0f)*ratio));
        state.actions[index]={action->action,std::wstring(action->name),
                              RECT{x,yy,xx,bottom}};
    }
    g.uiaProvider->update(std::move(state));
}

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
    g.mediaBridge.reconcile(g.activities,lynx::MediaObservation{
        next.available,next.playing,next.source,next.title,next.artist});
    g.mediaSnapshot=std::move(next);
}

void refresh();
void setExpanded(bool wantExpanded);

void showTransient(const std::wstring& text,UINT duration=2600) {
    g.transientToken=g.activities.transient(text);
    if(!g.transientToken)return;
    g.transientDeadline=GetTickCount64()+duration;
    SetTimer(g.hwnd,kTransientTimer,duration,nullptr);
}

void timerFocusTick() {
    const auto now=GetTickCount64();
    if(g.focus.state()!=lynx::FocusTimer::State::Running) {
        KillTimer(g.hwnd,kFocusTimer);
        return;
    }
    if(g.focus.advance(now)) {
        const auto selected=g.activities.primary();
        const bool ownExpanded=g.activities.expanded() &&
              selected && selected->id==lynx::FocusTimer::kId;
        if(ownExpanded) setExpanded(false); // terminal result may dismiss its own panel
        KillTimer(g.hwnd,kFocusTimer);
        g.focus.publish(g.activities,now);
        showTransient(L"Focus complete",3600);
        refresh();
        return;
    }
    const auto selected=g.activities.primary();
    const int remaining=g.focus.remainingSeconds(now);
    const auto newTitle=lynx::FocusTimer::clock(remaining);
    // Do not repaint the entire window for multiple timer callbacks per second.
    const bool visibleTimer = selected && selected->id==lynx::FocusTimer::kId;
    if(visibleTimer && (selected->title!=newTitle || g.activities.expanded())) {
        g.focus.publish(g.activities,now);
        refresh();
    } else if(!visibleTimer) {
        g.focus.publish(g.activities,now); // keep state current, without repaint
    }
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
    lynx::ScopedRenderSample timing(g.renderMetrics);
    updateDpiForMonitor();
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
            // All content stays within the interpolated shape while expanding.
            graphics.SetClip(&shape);

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
                    label(graphics,selected ? selected->subtitle :
                          (g.mediaSnapshot.error.empty() ? L"Ambient clock and power are live Windows data." :
                           L"Media unavailable - see log / permissions"),12,
                          x+23,y+94,Color(255,164,164,170));
                    if (selected && selected->id==lynx::FocusTimer::kId) {
                        GraphicsPath pauseButton,resetButton;
                        rounded(pauseButton,x+w-168.0f,y+125.0f,72.0f,31.0f,13.0f);
                        rounded(resetButton,x+w-88.0f,y+125.0f,66.0f,31.0f,13.0f);
                        SolidBrush buttonBrush(Color(255,39,39,44));
                        graphics.FillPath(&buttonBrush,&pauseButton);
                        graphics.FillPath(&buttonBrush,&resetButton);
                        label(graphics,g.focus.state()==lynx::FocusTimer::State::Running?
                            L"Pause":L"Resume",11,x+w-155.0f,y+130.0f,Color(255,245,245,247),true);
                        label(graphics,L"Reset",11,x+w-77.0f,y+130.0f,Color(255,245,245,247),true);
                    } else if (selected && selected->id==lynx::FileShelf::kId) {
                        GraphicsPath openButton,clearButton;
                        rounded(openButton,x+w-168.0f,y+125.0f,72.0f,31.0f,13.0f);
                        rounded(clearButton,x+w-88.0f,y+125.0f,66.0f,31.0f,13.0f);
                        SolidBrush buttonBrush(Color(255,39,39,44));
                        graphics.FillPath(&buttonBrush,&openButton);
                        graphics.FillPath(&buttonBrush,&clearButton);
                        label(graphics,L"Reveal",11,x+w-156.0f,y+130.0f,Color(255,245,245,247),true);
                        label(graphics,L"Clear",11,x+w-76.0f,y+130.0f,Color(255,245,245,247),true);
                    } else if (selected && selected->id==lynx::MediaActivityBridge::kId &&
                        g.mediaSnapshot.available && g.mediaSnapshot.canToggle) {
                        GraphicsPath action;
                        rounded(action,x+w-85.0f,y+125.0f,63.0f,31.0f,13.0f);
                        SolidBrush buttonBrush(Color(255,39,39,44));
                        graphics.FillPath(&buttonBrush,&action);
                        label(graphics,g.mediaSnapshot.playing ? L"Pause" : L"Play",
                              12,x+w-74.0f,y+130.0f,Color(255,245,245,247),true);
                    }
                    if(!selected)
                        label(graphics,L"Drop files here · Right-click for actions",11,
                              x+23,y+120,Color(255,140,140,148));
                    const auto actions=currentActions();
                    if(g.focusedAction>=0 && g.focusedAction<actions.count) {
                        const float buttonX=x+w-(actions.count==1?85.0f:
                            g.focusedAction==0?168.0f:88.0f);
                        const float buttonW=actions.count==1?63.0f:
                            g.focusedAction==0?72.0f:66.0f;
                        GraphicsPath focusContour;
                        rounded(focusContour,buttonX-2.0f,y+123.0f,
                                buttonW+4.0f,35.0f,14.0f);
                        Pen focusStroke(Color(255,10,132,255),1.8f);
                        graphics.DrawPath(&focusStroke,&focusContour);
                    }
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
            graphics.ResetClip();
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
    if (updated) {
        g.firstFrame = false;
        timing.success();
        syncAccessibleState();
    }

    SelectObject(memory,old);
    DeleteObject(dib);
    DeleteDC(memory);
    ReleaseDC(nullptr,screen);
    return updated != FALSE;
}

void refresh() {
    setTargets();
    drawFrame();
    syncAccessibleState(); // Hidden has no draw, but must disappear from UIA tree.
}

void setExpanded(bool wantExpanded) {
    g.focusedAction=-1;
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

void openActivity(std::uint32_t id) {
    if(g.activities.view()!=lynx::View::Expanded) setExpanded(true);
    if(g.activities.select(id)) refresh();
}
void startFocus(int seconds) {
    const auto now=GetTickCount64();
    g.focus.start(seconds,now);
    g.focus.publish(g.activities,now);
    SetTimer(g.hwnd,kFocusTimer,1000,nullptr);
    openActivity(lynx::FocusTimer::kId);
    refresh();
}
void toggleFocus() {
    const auto now=GetTickCount64();
    if(g.focus.state()==lynx::FocusTimer::State::Ready ||
       g.focus.state()==lynx::FocusTimer::State::Completed) {
        startFocus(25*60);
        return;
    }
    if(g.focus.state()==lynx::FocusTimer::State::Running) {
        const auto selected=g.activities.primary();
        const bool ownExpanded=g.activities.expanded() && selected &&
                                 selected->id==lynx::FocusTimer::kId;
        g.focus.pause(now);
        KillTimer(g.hwnd,kFocusTimer);
        // If deadline passed just before the Pause click, WM_TIMER has not
        // necessarily run yet. Complete through the same terminal path.
        if(g.focus.state()==lynx::FocusTimer::State::Completed) {
            if(ownExpanded) setExpanded(false);
            g.focus.publish(g.activities,now);
            showTransient(L"Focus complete",3600);
            refresh();
            return;
        }
    } else if(g.focus.resume(now)) {
        SetTimer(g.hwnd,kFocusTimer,1000,nullptr);
    }
    g.focus.publish(g.activities,now);
    refresh();
}
void resetFocus() {
    KillTimer(g.hwnd,kFocusTimer);
    g.focus.reset();
    g.focus.publish(g.activities,GetTickCount64());
    refresh();
}
void revealFirstFile() {
    const auto path=g.shelf.firstPath();
    if(path.empty())return;
    PIDLIST_ABSOLUTE file=nullptr;
    if(FAILED(SHParseDisplayName(path.c_str(),nullptr,&file,0,nullptr)) || !file)return;
    PIDLIST_ABSOLUTE folder=ILCloneFull(file);
    if(folder && ILRemoveLastID(folder)) {
        PCUITEMID_CHILD child=ILFindLastID(file);
        (void)SHOpenFolderAndSelectItems(folder,1,&child,0);
    }
    if(folder)CoTaskMemFree(folder);
    CoTaskMemFree(file);
}


void foregroundCallback(HWINEVENTHOOK, DWORD event, HWND window,
                        LONG object, LONG child, DWORD, DWORD) {
    if(event != EVENT_SYSTEM_FOREGROUND || object != OBJID_WINDOW ||
       child != CHILDID_SELF || window == g.hwnd || !g.hwnd)return;
    PostMessageW(g.hwnd,kForegroundMessage,0,0);
}

void reconcileMonitor() {
    MONITORINFO check{sizeof(check)};
    if(g.chosenMonitor && GetMonitorInfoW(g.chosenMonitor,&check))return;
    // The saved HMONITOR can be invalid after unplugging a display.
    // Choose a live display and never keep positioning against dead pixels.
    POINT pointer{};
    GetCursorPos(&pointer);
    g.chosenMonitor=MonitorFromPoint(pointer,MONITOR_DEFAULTTOPRIMARY);
}

void reconcileFullscreen() {
    reconcileMonitor();
    HWND foreground=GetForegroundWindow();
    if(foreground==g.hwnd)return; // LYNX may be active during an explicit action
    bool fullscreen=false;
    MONITORINFO monitor{sizeof(monitor)};
    if(foreground && IsWindow(foreground) && IsWindowVisible(foreground) &&
       !IsIconic(foreground) && GetMonitorInfoW(g.chosenMonitor,&monitor)) {
        wchar_t className[80]{};
        GetClassNameW(foreground,className,80);
        if(wcscmp(className,L"Progman")!=0 &&
           wcscmp(className,L"WorkerW")!=0 &&
           wcscmp(className,L"Shell_TrayWnd")!=0) {
            RECT bounds{};
            if(GetWindowRect(foreground,&bounds)) {
                fullscreen=lynx::coversMonitor(
                   {bounds.left,bounds.top,bounds.right,bounds.bottom},
                   {monitor.rcMonitor.left,monitor.rcMonitor.top,
                    monitor.rcMonitor.right,monitor.rcMonitor.bottom});
            }
        }
    }
    const bool desiredHidden=g.hiddenByDemo || fullscreen;
    if(fullscreen==g.actualFullscreen &&
       g.activities.fullscreen()==desiredHidden)return;
    g.actualFullscreen=fullscreen;
    if(desiredHidden && g.activities.expanded()) {
        // The foreground application already owns focus. Do not call
        // SetForegroundWindow during this automatic hide.
        g.activities.collapse();
        auto style=GetWindowLongPtrW(g.hwnd,GWL_EXSTYLE);
        SetWindowLongPtrW(g.hwnd,GWL_EXSTYLE,style | WS_EX_NOACTIVATE);
        g.priorFocus=nullptr;
    }
    g.activities.setFullscreen(desiredHidden);
    refresh();
}

void invokeContextAction(lynx::ShellAction kind) {
    const auto actions=currentActions();
    bool supported=false;
    for(int i=0;i<actions.count;i++) {
        if(actions.items[i].action==kind){supported=true;break;}
    }
    if(!supported)return; // client or queued event may refer to an old action
    switch(kind) {
        case lynx::ShellAction::MediaToggle:
            if(g.mediaProvider && g.mediaSnapshot.available && g.mediaSnapshot.canToggle)
                g.mediaProvider->togglePlayPause();
            break;
        case lynx::ShellAction::FocusToggle: toggleFocus();break;
        case lynx::ShellAction::FocusReset: resetFocus();break;
        case lynx::ShellAction::FileReveal: revealFirstFile();break;
        case lynx::ShellAction::FileClear:
            g.shelf.clear();
            g.shelf.reconcile(g.activities);
            refresh();
            break;
        default:break;
    }
}
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
#ifdef _DEBUG
    AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
    AppendMenuW(popup,MF_STRING,kAmbient,L"Clear simulated activities");
    AppendMenuW(popup,MF_STRING,kMediaDemo,L"Simulated media");
    AppendMenuW(popup,MF_STRING,kTimerDemo,L"Simulated focus");
#endif
    AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
    AppendMenuW(popup,MF_STRING,kFocus25,L"Start Focus · 25 min");
    AppendMenuW(popup,MF_STRING,kFocus5,L"Start Focus · 5 min");
    AppendMenuW(popup,MF_STRING | (
        g.focus.state()==lynx::FocusTimer::State::Running ||
        g.focus.state()==lynx::FocusTimer::State::Paused?MF_ENABLED:MF_GRAYED),
        kFocusPause,g.focus.state()==lynx::FocusTimer::State::Running?
        L"Pause Focus":L"Resume Focus");
    AppendMenuW(popup,MF_STRING,kFocusReset,L"Reset Focus");
    AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
    AppendMenuW(popup,MF_STRING | (g.shelf.files().empty()?MF_GRAYED:MF_ENABLED),
        kShelfOpen,L"Show File Shelf");
    AppendMenuW(popup,MF_STRING | (g.shelf.files().empty()?MF_GRAYED:MF_ENABLED),
        kShelfClear,L"Clear File Shelf");
    AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
    AppendMenuW(popup,MF_STRING |
                ((g.mediaSnapshot.available && g.mediaSnapshot.canToggle)?MF_ENABLED:MF_GRAYED),
                kToggleMedia,g.mediaSnapshot.playing?
                  L"Pause current Windows media":L"Play current Windows media");
#ifdef _DEBUG
    AppendMenuW(popup,MF_STRING,kTransientDemo,L"Simulate transient charging");
    AppendMenuW(popup,MF_STRING,kFullscreenDemo,
                g.hiddenByDemo?L"Restore from hidden":L"Simulate fullscreen hide");
#endif
    AppendMenuW(popup,MF_STRING | (g.reduced?MF_CHECKED:0),
                kReducedMotion,L"Reduced motion");
    AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
    AppendMenuW(popup,MF_STRING,kQuit,L"Quit LYNX");
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
            g.dpiMonitor=nullptr; // DPI must follow the destination display
            // Manual recovery retargets the overlay to an existing monitor;
            // fullscreen policy is still applied for its current foreground.
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
        case kFocus25: startFocus(25*60); break;
        case kFocus5: startFocus(5*60); break;
        case kFocusPause: toggleFocus(); break;
        case kFocusReset: resetFocus(); break;
        case kShelfOpen: openActivity(lynx::FileShelf::kId); break;
        case kShelfClear:
            g.shelf.clear();
            g.shelf.reconcile(g.activities);
            break;
        case kTransientDemo:
            showTransient(L"Power connected",2800);
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
            g.activities.setFullscreen(g.hiddenByDemo || g.actualFullscreen);
            break;
        case kQuit: DestroyWindow(hwnd); return;
        default: return;
    }
    // Track the application that was active *before* TrackPopupMenu.
    // The menu temporarily makes LYNX foreground; setExpanded(true) from
    // a tray command would otherwise capture LYNX itself as priorFocus.
    if (g.activities.view() == lynx::View::Expanded &&
        beforeMenuFocus != hwnd && beforeMenuFocus && IsWindow(beforeMenuFocus))
        g.priorFocus=beforeMenuFocus;
    if (g.activities.view() != lynx::View::Expanded) {
        const auto style=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);
        SetWindowLongPtrW(hwnd,GWL_EXSTYLE,style | WS_EX_NOACTIVATE);
        HWND restore=g.priorFocus ? g.priorFocus : beforeMenuFocus;
        if (restore && restore!=hwnd && IsWindow(restore)) SetForegroundWindow(restore);
        g.priorFocus=nullptr;
    }
    refresh();
    PostMessageW(hwnd,kForegroundMessage,0,0); // reconcile after menu focus is restored
}

void addTray(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize=sizeof(nid); nid.hWnd=hwnd; nid.uID=kTrayId;
    nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;
    nid.uCallbackMessage=kTrayMessage;
    nid.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
    wcscpy_s(nid.szTip,L"LYNX · Ctrl+Alt+Space");
    Shell_NotifyIconW(NIM_ADD,&nid);
}
void removeTray(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize=sizeof(nid); nid.hWnd=hwnd; nid.uID=kTrayId;
    Shell_NotifyIconW(NIM_DELETE,&nid);
}

LRESULT CALLBACK wndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(gTaskbarRestarted && msg==gTaskbarRestarted) {
        addTray(hwnd); // Explorer restarted: recover notification icon
        return 0;
    }
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
        case WM_LBUTTONUP: {
            if(g.activities.view()==lynx::View::Expanded) {
                const auto actions=currentActions();
                const float px=static_cast<float>(GET_X_LPARAM(lp))/scale();
                const float py=static_cast<float>(GET_Y_LPARAM(lp))/scale();
                for(int index=0;index<actions.count;index++) {
                    const float left=2.0f+g.width.value()-
                        (actions.count==1?85.0f:index==0?168.0f:88.0f);
                    const float width=actions.count==1?63.0f:index==0?72.0f:66.0f;
                    if(px>=left && px<left+width &&
                       py>=g.offset.value()+125.0f &&
                       py<g.offset.value()+156.0f) {
                        g.focusedAction=index;
                        invokeContextAction(actions.items[index].action);
                        syncAccessibleState();
                        return 0;
                    }
                }
            }
            toggleExpanded();
            return 0;
        }
        case WM_RBUTTONUP:
            menu(hwnd);
            return 0;
        case WM_HOTKEY:
            if (wp==kHotkeyId) toggleExpanded();
            return 0;
        case kUiInvokeMessage:
            toggleExpanded();
            return 0;
        case kUiActionMessage:
            invokeContextAction(static_cast<lynx::ShellAction>(wp));
            return 0;
        case kUiFocusMessage:
            if(g.activities.view()==lynx::View::Expanded) {
                const auto actions=currentActions();
                const int requested=static_cast<int>(wp);
                if(requested==-1 || (requested>=0 && requested<actions.count)) {
                    g.focusedAction=requested;
                    SetForegroundWindow(hwnd); // only explicit UIA focus request
                    SetFocus(hwnd);
                    refresh();
                }
            }
            return 0;
        case kVolumeMessage: {
            if(!g.volume)return 0;
            if(wp==1) {
                // A headset/unplug/speaker switch resets the HUD baseline.
                // Do not announce a spurious volume change across devices.
                if(!g.volume->rebind())diagnostic("volume endpoint unavailable");
                g.volumeState=g.volume->read();
                g.volumeInitialized=g.volumeState.available;
                return 0;
            }
            const auto now=g.volume->read();
            if(!now.available)return 0;
            const bool changed=g.volumeInitialized &&
              (now.percent!=g.volumeState.percent || now.muted!=g.volumeState.muted);
            g.volumeState=now;
            g.volumeInitialized=true;
            if(changed) {
                showTransient(now.muted?L"Muted":
                    (L"Volume " + std::to_wstring(now.percent) + L"%"),1450);
                refresh();
            }
            return 0;
        }
        case kMediaMessage: {
            std::unique_ptr<lynx::MediaSnapshot> media(
                 reinterpret_cast<lynx::MediaSnapshot*>(lp));
            if (!media) return 0;
            if (!media->error.empty()) diagnostic("GSMTC unavailable");
            const auto previousView=g.activities.view();
            const auto oldSource=g.mediaSnapshot.source;
            const auto oldAvailable=g.mediaSnapshot.available;
            const auto oldPlaying=g.mediaSnapshot.playing;
            const auto oldTitle=g.mediaSnapshot.title;
            const auto oldArtist=g.mediaSnapshot.artist;
            const auto oldCanToggle=g.mediaSnapshot.canToggle;
            const auto oldError=g.mediaSnapshot.error;
            onMediaSnapshot(std::move(*media));
            if (previousView!=g.activities.view() ||
                oldSource!=g.mediaSnapshot.source ||
                oldAvailable!=g.mediaSnapshot.available ||
                oldPlaying!=g.mediaSnapshot.playing ||
                oldTitle!=g.mediaSnapshot.title ||
                oldArtist!=g.mediaSnapshot.artist ||
                oldCanToggle!=g.mediaSnapshot.canToggle ||
                oldError!=g.mediaSnapshot.error)
                refresh();
            return 0;
        }
        case WM_GETOBJECT:
            if (static_cast<LONG>(lp)==UiaRootObjectId) {
                if (!g.uiaProvider) {
                    g.uiaProvider=new (std::nothrow) lynx::ShellUiaRoot(
                        hwnd,kUiInvokeMessage,kUiActionMessage,kUiFocusMessage);
                    if(g.uiaProvider) syncAccessibleState();
                }
                if (g.uiaProvider)
                    return UiaReturnRawElementProvider(hwnd,wp,lp,
                        static_cast<IRawElementProviderSimple*>(g.uiaProvider));
            }
            break;
        case WM_KEYDOWN:
            if(g.activities.view()==lynx::View::Expanded) {
                if(wp==VK_ESCAPE) {
                    setExpanded(false);
                    return 0;
                }
                const auto actions=currentActions();
                if(wp==VK_TAB && actions.count>0) {
                    const bool reverse=(GetKeyState(VK_SHIFT)&0x8000)!=0;
                    g.focusedAction=reverse ?
                        (g.focusedAction<0?actions.count-1:
                         (g.focusedAction+actions.count-1)%actions.count) :
                        (g.focusedAction+1)%actions.count;
                    refresh();
                    return 0;
                }
                if((wp==VK_RETURN || wp==VK_SPACE) && actions.count>0) {
                    const int selected=g.focusedAction<0?0:g.focusedAction;
                    if(auto action=actions.at(selected)) {
                        invokeContextAction(action->action);
                        return 0;
                    }
                }
                if(wp==VK_RETURN && actions.count==0) {
                    setExpanded(false);
                    return 0;
                }
            }
            break;
        case WM_TIMER:
            if (wp==kFocusTimer) {timerFocusTick();return 0;}
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
            if(wp==PBT_APMRESUMEAUTOMATIC || wp==PBT_APMRESUMESUSPEND) {
                // Monotonic deadlines include sleep: reconcile immediately,
                // not after the next one-second WM_TIMER.
                timerFocusTick();
                PostMessageW(hwnd,kForegroundMessage,0,0);
            }
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
                        showTransient(L"Power connected",2800);
                    }
                    refresh();
                }
                return TRUE;
            }
            break;
        case WM_DISPLAYCHANGE:
            reconcileMonitor();
            g.dpiMonitor=nullptr;
            reconcileFullscreen();
            drawFrame();
            return 0;
        case WM_DPICHANGED:
            g.dpiMonitor=nullptr;
            drawFrame();
            return 0;
        case kForegroundMessage:
            reconcileFullscreen();
            return 0;
        case WM_SETTINGCHANGE:
            drawFrame(); return 0;
        case kTrayMessage:
            if (lp==WM_RBUTTONUP || lp==WM_CONTEXTMENU) menu(hwnd);
            else if (lp==WM_LBUTTONDBLCLK) toggleExpanded();
            return 0;
        case WM_DESTROY:
            if(g.foregroundHook) {
                UnhookWinEvent(g.foregroundHook);
                g.foregroundHook=nullptr;
            }
            g.volume.reset(); // unregister Core Audio callbacks before COM teardown
            if(g.dropRegistered) {
                RevokeDragDrop(hwnd);
                g.dropRegistered=false;
            }
            if(g.dropTarget) {
                g.dropTarget->Release();
                g.dropTarget=nullptr;
            }
            KillTimer(hwnd,kFocusTimer);
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
                g.uiaProvider->invalidate();
                UiaReturnRawElementProvider(hwnd,0,0,nullptr);
                g.uiaProvider->Release();
                g.uiaProvider=nullptr;
            }
            g.renderMetrics.exportCsv();
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
    // OLE drag/drop is a UI-STA facility. Do not initialize this thread as MTA.
    const HRESULT oleResult=OleInitialize(nullptr);
    const bool oleReady=SUCCEEDED(oleResult);
    GdiplusStartupInput startup{};
    if (GdiplusStartup(&g.gdiplus,&startup,nullptr)!=Ok) {
        if(oleReady)OleUninitialize();
        CloseHandle(singleton); return 1;
    }
    gTaskbarRestarted=RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc=wndProc; wc.hInstance=instance;
    wc.lpszClassName=L"LYNX_M0_NATIVE_OVERLAY";
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if (!RegisterClassExW(&wc)) {
        GdiplusShutdown(g.gdiplus);
        if(oleReady)OleUninitialize();
        CloseHandle(singleton); return 2;
    }
    HWND hwnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_NOACTIVATE,
         wc.lpszClassName,L"LYNX M0",WS_POPUP,0,0,440,260,
         nullptr,nullptr,instance,nullptr);
    if (!hwnd) {
        GdiplusShutdown(g.gdiplus);
        if(oleReady)OleUninitialize();
        CloseHandle(singleton); return 3;
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
        if(oleReady)OleUninitialize();
        CloseHandle(singleton);
        return 4;
    }
    ShowWindow(hwnd,SW_SHOWNOACTIVATE);
    SetWindowPos(hwnd,HWND_TOPMOST,0,0,0,0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    diagnostic("startup overlay shown");
    g.foregroundHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,
        nullptr,foregroundCallback,0,0,WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if(!g.foregroundHook)diagnostic("foreground hook failed",GetLastError());
    reconcileFullscreen();
    if(oleReady) {
        g.dropTarget=new (std::nothrow) lynx::FileDropTarget(
            [hwnd](std::vector<std::wstring> paths) {
                bool added=false;
                for(const auto& path:paths) {
                    const DWORD attrs=GetFileAttributesW(path.c_str());
                    if(attrs!=INVALID_FILE_ATTRIBUTES)
                        added=g.shelf.add(path) || added;
                }
                if(added) {
                    g.shelf.reconcile(g.activities);
                    showTransient(L"File added",1800);
                    refresh();
                }
                return added; // reject duplicate/full/unavailable drops
            });
        if(g.dropTarget) {
            const HRESULT registered=RegisterDragDrop(hwnd,g.dropTarget);
            g.dropRegistered=SUCCEEDED(registered);
            if(!g.dropRegistered)diagnostic("RegisterDragDrop failed",
                static_cast<DWORD>(registered));
        }
        g.volume=std::make_unique<lynx::WindowsVolumeFeedback>(
            hwnd,kVolumeMessage);
        g.volumeState=g.volume->read();
        g.volumeInitialized=g.volumeState.available;
    } else {
        diagnostic("OleInitialize failed",static_cast<DWORD>(oleResult));
    }
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
    if(oleReady)OleUninitialize();
    CloseHandle(singleton);
    return static_cast<int>(msg.wParam);
}
