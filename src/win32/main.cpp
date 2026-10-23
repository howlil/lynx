#include "../core/island_core.hpp"

#include <windows.h>
#include <objbase.h>
#include <objidl.h>
#include <oleauto.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

using namespace Gdiplus;

namespace {
constexpr UINT_PTR kAnimationTimer = 101;
constexpr UINT_PTR kClockTimer = 102;
constexpr UINT_PTR kTransientTimer = 103;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayId = 1;
constexpr int kHotkeyId = 1;
constexpr float kCanvasWidthDip = 440.0f;
constexpr float kCanvasHeightDip = 260.0f;

enum MenuId : UINT {
    kFloating = 2001, kAttached, kAmbient, kMediaDemo,
    kTransientDemo, kReducedMotion, kFullscreenDemo, kQuit
};

struct App {
    HWND hwnd = nullptr;
    ULONG_PTR gdiplus = 0;
    lynx::Placement placement = lynx::Placement::Floating;
    lynx::Presentation presentation;
    lynx::Spring width{232}, height{42}, shoulder{0}, offset{13};
    bool reduced = false;
    bool demoMedia = false;
    bool hiddenByDemo = false;
    HWND priorFocus = nullptr;
    int dpi = 96;
    ULONGLONG previousFrame = 0;
};
App g;

float scale() { return static_cast<float>(g.dpi) / 96.0f; }

void timerAtMinuteBoundary() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const UINT millis = std::max<UINT>(250, 60000u - now.wSecond * 1000u - now.wMilliseconds);
    SetTimer(g.hwnd, kClockTimer, millis, nullptr);
}

void setTargets() {
    const auto geo = lynx::target(g.placement, g.presentation.view());
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
    if (g.presentation.view() == lynx::View::Hidden) {
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

void drawFrame() {
    if (g.presentation.view() == lynx::View::Hidden) return;
    const float dpi = scale();
    const int W = static_cast<int>(std::ceil(kCanvasWidthDip * dpi));
    const int H = static_cast<int>(std::ceil(kCanvasHeightDip * dpi));
    if (W <= 0 || H <= 0) return;

    HDC screen = GetDC(nullptr);
    if (!screen) return;
    HDC memory = CreateCompatibleDC(screen);
    if (!memory) { ReleaseDC(nullptr,screen); return; }

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
        DeleteDC(memory);
        ReleaseDC(nullptr,screen);
        return;
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
            const float x = (kCanvasWidthDip-w)*0.5f;
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
            const auto currentView = g.presentation.view();

            if (currentView == lynx::View::Ambient || currentView == lynx::View::Expanded) {
                SYSTEMTIME now{};
                GetLocalTime(&now);
                wchar_t clockText[12]{};
                swprintf_s(clockText, L"%02u:%02u", now.wHour, now.wMinute);
                label(graphics,clockText,14.0f,contentLeft,y+10.0f,Color(255,245,245,247),true);

                const float center = x+w*0.5f;
                SolidBrush lens(Color(255,17,17,20));
                Pen lensStroke(Color(255,30,30,35),0.8f);
                graphics.FillEllipse(&lens,center-17,y+16,34,10);
                graphics.DrawEllipse(&lensStroke,center-17,y+16,34,10);

                SYSTEM_POWER_STATUS power{};
                const bool read = GetSystemPowerStatus(&power) != FALSE;
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
                    graphics.DrawRectangle(&batteryPen,contentRight-18,y+15,15,10);
                    graphics.DrawLine(&batteryPen,contentRight-1,y+18,contentRight-1,y+22);
                    if (read && power.BatteryLifePercent != 255 && power.BatteryFlag != 128) {
                        SolidBrush chargeFill(accent);
                        graphics.FillRectangle(&chargeFill,contentRight-16,y+17,
                            11.0f*(power.BatteryLifePercent/100.0f),6.0f);
                    }
                }
                if (currentView == lynx::View::Expanded) {
                    Pen divider(Color(255,40,40,45),1.0f);
                    graphics.DrawLine(&divider,x+20,y+48,x+w-20,y+48);
                    label(graphics,L"LYNX  /  NATIVE SHELL M0",13,x+23,y+64,
                          Color(255,245,245,247),true);
                    label(graphics,L"Ambient clock and power are live Windows data.",12,
                          x+23,y+94,Color(255,164,164,170));
                    label(graphics,L"Click to collapse  |  Right-click for scenarios",11,
                          x+23,y+120,Color(255,140,140,148));
                    label(graphics,L"Renderer: layered alpha spike; compositor undecided",10,
                          x+23,y+153,Color(255,115,115,122));
                }
            } else if (currentView == lynx::View::Compact) {
                SolidBrush cover(Color(255,68,97,165));
                graphics.FillEllipse(&cover,x+shoulder+10,y+8,26,26);
                label(graphics,L"Now playing",12,x+shoulder+46,y+10,
                      Color(255,245,245,247),true);
                label(graphics,L"Demo",10,contentRight-36,y+13,
                      Color(255,164,164,170));
            } else if (currentView == lynx::View::Transient) {
                label(graphics,L"Power connected",13,contentLeft,y+15,
                      Color(255,48,209,88),true);
            }
        }
    }

    MONITORINFO monitor{sizeof(MONITORINFO)};
    HMONITOR display = MonitorFromWindow(g.hwnd, MONITOR_DEFAULTTOPRIMARY);
    if (!GetMonitorInfoW(display,&monitor)) {
        monitor.rcMonitor = RECT{0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
    }
    POINT dst{monitor.rcMonitor.left +
                  ((monitor.rcMonitor.right-monitor.rcMonitor.left)-W)/2,
              monitor.rcMonitor.top};
    POINT src{0,0};
    SIZE size{W,H};
    BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    UpdateLayeredWindow(g.hwnd, screen, &dst, &size, memory, &src, 0, &blend, ULW_ALPHA);

    SelectObject(memory,old);
    DeleteObject(dib);
    DeleteDC(memory);
    ReleaseDC(nullptr,screen);
}

void refresh() { setTargets(); drawFrame(); }

void setExpanded(bool wantExpanded) {
    if (wantExpanded) {
        g.priorFocus = GetForegroundWindow();
        g.presentation.expand();
        // Intentional expansion may become keyboard-active; passive compact does not.
        auto style = GetWindowLongPtrW(g.hwnd,GWL_EXSTYLE);
        SetWindowLongPtrW(g.hwnd,GWL_EXSTYLE,style & ~WS_EX_NOACTIVATE);
        refresh();
        SetForegroundWindow(g.hwnd);
        SetFocus(g.hwnd);
    } else {
        g.presentation.collapse();
        const auto style = GetWindowLongPtrW(g.hwnd,GWL_EXSTYLE);
        SetWindowLongPtrW(g.hwnd,GWL_EXSTYLE,style | WS_EX_NOACTIVATE);
        refresh();
        if (g.priorFocus && IsWindow(g.priorFocus)) SetForegroundWindow(g.priorFocus);
        g.priorFocus = nullptr;
    }
}

void toggleExpanded() { setExpanded(g.presentation.view() != lynx::View::Expanded); }

void menu(HWND hwnd) {
    HMENU popup = CreatePopupMenu();
    if (!popup) return;
    AppendMenuW(popup, MF_STRING | (g.placement==lynx::Placement::Floating?MF_CHECKED:0),
                kFloating,L"Floating");
    AppendMenuW(popup, MF_STRING | (g.placement==lynx::Placement::Attached?MF_CHECKED:0),
                kAttached,L"Top-attached");
    AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
    AppendMenuW(popup,MF_STRING,kAmbient,L"Ambient (live clock / battery)");
    AppendMenuW(popup,MF_STRING,kMediaDemo,L"Simulate compact media");
    AppendMenuW(popup,MF_STRING,kTransientDemo,L"Simulate transient charging");
    AppendMenuW(popup,MF_STRING | (g.reduced?MF_CHECKED:0),
                kReducedMotion,L"Reduced motion");
    AppendMenuW(popup,MF_STRING,kFullscreenDemo,
                g.hiddenByDemo?L"Restore from hidden":L"Simulate fullscreen hide");
    AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
    AppendMenuW(popup,MF_STRING,kQuit,L"Quit LYNX M0");
    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    const UINT command=TrackPopupMenu(popup,TPM_RETURNCMD|TPM_RIGHTBUTTON,
                                       pt.x,pt.y,0,hwnd,nullptr);
    DestroyMenu(popup);
    PostMessageW(hwnd,WM_NULL,0,0);
    switch (command) {
        case kFloating: g.placement=lynx::Placement::Floating; break;
        case kAttached: g.placement=lynx::Placement::Attached; break;
        case kAmbient: g.demoMedia=false; g.presentation.ambient(); break;
        case kMediaDemo: g.demoMedia=true; g.presentation.compact(); break;
        case kTransientDemo:
            g.presentation.transient();
            SetTimer(hwnd,kTransientTimer,2800,nullptr);
            break;
        case kReducedMotion: g.reduced=!g.reduced; break;
        case kFullscreenDemo:
            g.hiddenByDemo=!g.hiddenByDemo;
            g.presentation.setFullscreen(g.hiddenByDemo);
            break;
        case kQuit: DestroyWindow(hwnd); return;
        default: return;
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
            RegisterHotKey(hwnd,kHotkeyId,MOD_CONTROL|MOD_ALT,VK_SPACE);
            addTray(hwnd);
            timerAtMinuteBoundary();
            refresh();
            return 0;
        }
        case WM_LBUTTONUP:
            toggleExpanded();
            return 0;
        case WM_RBUTTONUP:
            menu(hwnd);
            return 0;
        case WM_HOTKEY:
            if (wp==kHotkeyId) toggleExpanded();
            return 0;
        case WM_KEYDOWN:
            if (wp==VK_ESCAPE && g.presentation.view()==lynx::View::Expanded) {
                setExpanded(false);
                return 0;
            }
            break;
        case WM_TIMER:
            if (wp==kClockTimer) { timerAtMinuteBoundary(); drawFrame(); return 0; }
            if (wp==kTransientTimer) {
                KillTimer(hwnd,kTransientTimer);
                g.presentation.expireTransient(); refresh(); return 0;
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
        case WM_POWERBROADCAST:
            if (msg==WM_POWERBROADCAST && wp!=PBT_APMPOWERSTATUSCHANGE &&
                wp!=PBT_APMRESUMEAUTOMATIC && wp!=PBT_APMRESUMESUSPEND) break;
            timerAtMinuteBoundary();
            drawFrame();
            return TRUE;
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
    if (!singleton || GetLastError()==ERROR_ALREADY_EXISTS) {
        if (singleton) CloseHandle(singleton);
        return 0;
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
    ShowWindow(hwnd,SW_SHOWNOACTIVATE);
    drawFrame();
    MSG msg{};
    while(GetMessageW(&msg,nullptr,0,0)>0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    GdiplusShutdown(g.gdiplus);
    CloseHandle(singleton);
    return static_cast<int>(msg.wParam);
}
