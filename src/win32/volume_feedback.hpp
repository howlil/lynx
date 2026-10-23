#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <atomic>
#include <memory>
#include <algorithm>

namespace lynx {
class WindowsVolumeFeedback {
public:
    struct Status { bool available=false, muted=false; unsigned percent=0; };
    WindowsVolumeFeedback(HWND hwnd, UINT notify)
        : callback_(new Callback(hwnd,notify)) {
        // Must run on the OleInitialize UI apartment, not the GSMTC worker.
        if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&enumerator_))))return;
        IMMDevice* device=nullptr;
        if(SUCCEEDED(enumerator_->GetDefaultAudioEndpoint(eRender,eMultimedia,&device))) {
            if(device) {
                (void)device->Activate(__uuidof(IAudioEndpointVolume),
                      CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&endpoint_));
                device->Release();
            }
        }
        if(endpoint_) subscribed_=SUCCEEDED(endpoint_->RegisterControlChangeNotify(callback_));
    }
    ~WindowsVolumeFeedback() {
        if(endpoint_) {
            if(subscribed_) endpoint_->UnregisterControlChangeNotify(callback_);
            endpoint_->Release();
        }
        if(enumerator_) enumerator_->Release();
        callback_->Release();
    }
    WindowsVolumeFeedback(const WindowsVolumeFeedback&)=delete;
    WindowsVolumeFeedback& operator=(const WindowsVolumeFeedback&)=delete;
    Status read() const {
        Status s{};
        if(!endpoint_)return s;
        float scalar=0;
        BOOL mute=FALSE;
        if(FAILED(endpoint_->GetMasterVolumeLevelScalar(&scalar)) ||
           FAILED(endpoint_->GetMute(&mute)))return s;
        s.available=true;
        s.muted=(mute!=FALSE);
        s.percent=static_cast<unsigned>(std::clamp(scalar,0.0f,1.0f)*100.0f+0.5f);
        return s;
    }
private:
    class Callback final : public IAudioEndpointVolumeCallback {
    public:
        Callback(HWND h,UINT m):hwnd_(h),message_(m){}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
            if(!out)return E_POINTER;
            *out=nullptr;
            if(iid==__uuidof(IUnknown)||iid==__uuidof(IAudioEndpointVolumeCallback))
                *out=static_cast<IAudioEndpointVolumeCallback*>(this);
            else return E_NOINTERFACE;
            AddRef();return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() override {return ++refs_;}
        ULONG STDMETHODCALLTYPE Release() override {
            auto remaining=--refs_;
            if(!remaining)delete this;
            return remaining;
        }
        HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data) override {
            if(data) PostMessageW(hwnd_,message_,0,0); // non-blocking callback
            return S_OK;
        }
    private:
        ~Callback()=default;
        std::atomic<ULONG> refs_{1};
        HWND hwnd_;
        UINT message_;
    };
    IMMDeviceEnumerator* enumerator_=nullptr;
    IAudioEndpointVolume* endpoint_=nullptr;
    Callback* callback_=nullptr;
    bool subscribed_=false;
};
} // namespace lynx
