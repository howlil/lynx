#pragma once
#include "../core/island_core.hpp"
#include <windows.h>

namespace lynx {

struct UserPreferences {
    Placement placement = Placement::Floating;
    bool reducedMotion = false;
    bool hasReducedMotionOverride = false;
};

class WindowsSettingsStore {
public:
    static UserPreferences load() {
        UserPreferences p;
        DWORD raw = 0;
        if (read(L"Placement", raw)) {
            if (raw <= 1) p.placement = raw == 1 ? Placement::Attached : Placement::Floating;
        }
        if (read(L"ReducedMotion", raw) && raw <= 1) {
            p.hasReducedMotionOverride = true;
            p.reducedMotion = raw == 1;
        }
        return p;
    }

    static bool savePlacement(Placement placement) {
        return write(L"Placement", placement == Placement::Attached ? 1u : 0u);
    }
    static bool saveReducedMotion(bool enabled) {
        return write(L"ReducedMotion", enabled ? 1u : 0u);
    }
private:
    static constexpr const wchar_t* kSubkey = L"Software\\LYNX";

    static bool read(const wchar_t* name, DWORD& out) {
        DWORD type = 0, size = sizeof(out);
        return RegGetValueW(HKEY_CURRENT_USER,kSubkey,name,RRF_RT_REG_DWORD,
                            &type,&out,&size) == ERROR_SUCCESS && size == sizeof(out);
    }
    static bool write(const wchar_t* name, DWORD value) {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER,kSubkey,0,nullptr,0,
                            KEY_SET_VALUE,nullptr,&key,nullptr) != ERROR_SUCCESS) return false;
        const LONG status = RegSetValueExW(key,name,0,REG_DWORD,
                             reinterpret_cast<const BYTE*>(&value),sizeof(value));
        RegCloseKey(key);
        return status == ERROR_SUCCESS;
    }
};
} // namespace lynx
