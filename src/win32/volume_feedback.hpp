#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <algorithm>
#include <atomic>

namespace lynx {

// All COM object binding/rebinding occurs on the window's OLE STA.
// Core Audio callbacks only post a small notification to the HWND; they
// never call back into COM or mutate any activity/render state.
class WindowsVolumeFeedback {
public:
    struct Status { bool available=false, muted=false; unsigned percent=0; };
    WindowsVolumeFeedback(HWND hwnd,UINT notification)
        : callback_(new Callback(hwnd,notification)) {
        if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&enumerator_)))) return;
        subscribedDevices_=SUCCEEDED(
            enumerator_->RegisterEndpointNotificationCallback(callback_));
        rebind();
    }
    ~WindowsVolumeFeedback() {
        if(enumerator_ && subscribedDevices_)
            enumerator_->UnregisterEndpointNotificationCallback(callback_);
        releaseEndpoint();
        if(enumerator_)enumerator_->Release();
        callback_->Release();
    }
    WindowsVolumeFeedback(const WindowsVolumeFeedback&)=delete;
    WindowsVolumeFeedback& operator=(const WindowsVolumeFeedback&)=delete;

    // Invoked on UI thread for device-change notifications. A missing output
    // is normal; next default-device change will try again.
    bool rebind() {
        releaseEndpoint();
        if(!enumerator_)return false;
        IMMDevice* device=nullptr;
        if(FAILED(enumerator_->GetDefaultAudioEndpoint(eRender,eMultimedia,&device)))
            return false;
        if(!device)return false;
        const HRESULT activated=device->Activate(__uuidof(IAudioEndpointVolume),
                         CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&endpoint_));
        device->Release();
        if(FAILED(activated) || !endpoint_)return false;
        subscribedVolume_=SUCCEEDED(
            endpoint_->RegisterControlChangeNotify(callback_));
        return subscribedVolume_;
    }
    Status read() const {
        Status s{};
        if(!endpoint_)return s;
        float scalar=0;
        BOOL muted=FALSE;
        if(FAILED(endpoint_->GetMasterVolumeLevelScalar(&scalar)) ||
           FAILED(endpoint_->GetMute(&muted)))return s;
        s.available=true;s.muted=muted!=FALSE;
        s.percent=static_cast<unsigned>(std::clamp(scalar,0.0f,1.0f)*100.0f+0.5f);
        return s;
    }
private:
    void releaseEndpoint() {
        if(endpoint_) {
            if(subscribedVolume_)endpoint_->UnregisterControlChangeNotify(callback_);
            endpoint_->Release();
            endpoint_=nullptr;
        }
        subscribedVolume_=false;
    }

    class Callback final : public IAudioEndpointVolumeCallback,
                           public IMMNotificationClient {
    public:
        Callback(HWND hwnd,UINT msg):hwnd_(hwnd),message_(msg){}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** output) override {
            if(!output)return E_POINTER;
            *output=nullptr;
            if(iid==__uuidof(IUnknown) ||
               iid==__uuidof(IAudioEndpointVolumeCallback))
                *output=static_cast<IAudioEndpointVolumeCallback*>(this);
            else if(iid==__uuidof(IMMNotificationClient))
                *output=static_cast<IMMNotificationClient*>(this);
            else return E_NOINTERFACE;
            AddRef();return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}
        ULONG STDMETHODCALLTYPE Release() override{
            const ULONG n=--refs_;
            if(!n)delete this;
            return n;
        }
        HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data) override {
            if(data)PostMessageW(hwnd_,message_,0,0);
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(
            EDataFlow flow,ERole role,LPCWSTR) override {
            if(flow==eRender && role==eMultimedia)
                PostMessageW(hwnd_,message_,1,0);
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR,DWORD) override {
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override {return S_OK;}
        HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override {return S_OK;}
        HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR,
                                                        const PROPERTYKEY) override {
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
    Callback* callback_;
    bool subscribedDevices_=false,subscribedVolume_=false;
};
} // namespace lynx
