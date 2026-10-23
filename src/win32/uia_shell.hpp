#pragma once
#include "../core/context_actions.hpp"
#include <windows.h>
#include <UIAutomation.h>
#include <oleauto.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace lynx {

struct AccessibleAction {
    ShellAction kind=ShellAction::None;
    std::wstring name;
    RECT screen{};
};
struct AccessibleSnapshot {
    bool visible=true;
    bool expanded=false;
    int focused=-1;
    int count=0;
    std::wstring name=L"LYNX Island";
    RECT screen{};
    std::array<AccessibleAction,2> actions{};
};

inline HRESULT setUiaName(VARIANT* value,const std::wstring& name) {
    value->bstrVal=SysAllocString(name.c_str());
    if(!value->bstrVal)return E_OUTOFMEMORY;
    value->vt=VT_BSTR;
    return S_OK;
}

class ShellUiaButton;

// Root exposes a real fragment so child context actions appear individually
// in Inspect/Accessibility Insights, unlike the original single UIA button.
class ShellUiaRoot final : public IRawElementProviderSimple,
                           public IRawElementProviderFragment,
                           public IRawElementProviderFragmentRoot,
                           public IInvokeProvider {
public:
    ShellUiaRoot(HWND hwnd,UINT toggleMessage,UINT actionMessage,UINT focusMessage)
        : hwnd_(hwnd),toggle_(toggleMessage),action_(actionMessage),focus_(focusMessage){}

    void update(AccessibleSnapshot next) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_=std::move(next);
    }
    AccessibleSnapshot snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }
    void invalidate(){hwnd_=nullptr;}
    HWND hwnd() const {return hwnd_.load();}
    UINT actionMessage() const {return action_;}
    UINT focusMessage() const {return focus_;}
    HRESULT createChild(int index,IRawElementProviderFragment** result);

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** output) override {
        if(!output)return E_POINTER;
        *output=nullptr;
        if(iid==__uuidof(IUnknown) || iid==__uuidof(IRawElementProviderSimple))
            *output=static_cast<IRawElementProviderSimple*>(this);
        else if(iid==__uuidof(IRawElementProviderFragment))
            *output=static_cast<IRawElementProviderFragment*>(this);
        else if(iid==__uuidof(IRawElementProviderFragmentRoot))
            *output=static_cast<IRawElementProviderFragmentRoot*>(this);
        else if(iid==__uuidof(IInvokeProvider))
            *output=static_cast<IInvokeProvider*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references_;}
    ULONG STDMETHODCALLTYPE Release() override {
        auto count=--references_;
        if(!count)delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* options) override {
        if(!options)return E_POINTER;
        *options=ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id,IUnknown** output) override {
        if(!output)return E_POINTER;
        *output=nullptr;
        if(id==UIA_InvokePatternId) {
            *output=static_cast<IInvokeProvider*>(this);
            AddRef();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id,VARIANT* v) override {
        if(!v)return E_POINTER;
        VariantInit(v);
        auto s=snapshot();
        switch(id) {
            case UIA_ControlTypePropertyId:
                v->vt=VT_I4;v->lVal=UIA_PaneControlTypeId;return S_OK;
            case UIA_NamePropertyId:return setUiaName(v,s.name);
            case UIA_AutomationIdPropertyId:
                return setUiaName(v,L"lynx.island.root");
            case UIA_IsControlElementPropertyId:
            case UIA_IsContentElementPropertyId:
                v->vt=VT_BOOL;v->boolVal=VARIANT_TRUE;return S_OK;
            case UIA_IsOffscreenPropertyId:
                v->vt=VT_BOOL;v->boolVal=s.visible?VARIANT_FALSE:VARIANT_TRUE;
                return S_OK;
            case UIA_IsEnabledPropertyId:
                v->vt=VT_BOOL;v->boolVal=s.visible?VARIANT_TRUE:VARIANT_FALSE;
                return S_OK;
            case UIA_IsKeyboardFocusablePropertyId:
                v->vt=VT_BOOL;v->boolVal=s.expanded?VARIANT_TRUE:VARIANT_FALSE;
                return S_OK;
            default:return S_OK;
        }
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
        IRawElementProviderSimple** result) override {
        if(!result)return E_POINTER;
        *result=nullptr;
        HWND h=hwnd();
        return h?UiaHostProviderFromHwnd(h,result):UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                      IRawElementProviderFragment** result) override {
        if(!result)return E_POINTER;
        *result=nullptr;
        const auto s=snapshot();
        if(s.count==0)return S_OK;
        if(direction==NavigateDirection_FirstChild)return createChild(0,result);
        if(direction==NavigateDirection_LastChild)return createChild(s.count-1,result);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** id) override {
        if(!id)return E_POINTER;
        *id=nullptr; // Host HWND defines root runtime ID
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* result) override {
        if(!result)return E_POINTER;
        const auto s=snapshot();
        result->left=s.screen.left;result->top=s.screen.top;
        result->width=s.screen.right-s.screen.left;
        result->height=s.screen.bottom-s.screen.top;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** roots) override {
        if(!roots)return E_POINTER;
        *roots=nullptr;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override {
        HWND h=hwnd();
        return h && PostMessageW(h,focus_,static_cast<WPARAM>(-1),0)
            ? S_OK:UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** result) override {
        if(!result)return E_POINTER;
        *result=static_cast<IRawElementProviderFragmentRoot*>(this);
        AddRef();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x,double y,
                                               IRawElementProviderFragment** result) override {
        if(!result)return E_POINTER;
        *result=nullptr;
        const auto s=snapshot();
        for(int i=0;i<s.count;i++) {
            auto r=s.actions[i].screen;
            if(x>=r.left && x<r.right && y>=r.top && y<r.bottom)
                return createChild(i,result);
        }
        *result=static_cast<IRawElementProviderFragment*>(this);
        AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** result) override {
        if(!result)return E_POINTER;
        *result=nullptr;
        const auto s=snapshot();
        if(s.expanded && s.focused>=0 && s.focused<s.count)
            return createChild(s.focused,result);
        *result=static_cast<IRawElementProviderFragment*>(this);
        AddRef();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke() override {
        HWND h=hwnd();
        return h && PostMessageW(h,toggle_,0,0)?S_OK:UIA_E_ELEMENTNOTAVAILABLE;
    }
private:
    ~ShellUiaRoot()=default;
    mutable std::mutex mutex_;
    AccessibleSnapshot state_{};
    std::atomic<HWND> hwnd_;
    const UINT toggle_,action_,focus_;
    std::atomic<ULONG> references_{1};
};

class ShellUiaButton final : public IRawElementProviderSimple,
                             public IRawElementProviderFragment,
                             public IInvokeProvider {
public:
    ShellUiaButton(ShellUiaRoot* root,int index):root_(root),index_(index){
        root_->AddRef();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** output) override {
        if(!output)return E_POINTER;
        *output=nullptr;
        if(iid==__uuidof(IUnknown) || iid==__uuidof(IRawElementProviderSimple))
            *output=static_cast<IRawElementProviderSimple*>(this);
        else if(iid==__uuidof(IRawElementProviderFragment))
            *output=static_cast<IRawElementProviderFragment*>(this);
        else if(iid==__uuidof(IInvokeProvider))
            *output=static_cast<IInvokeProvider*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++references_;}
    ULONG STDMETHODCALLTYPE Release() override{
        auto n=--references_;if(!n)delete this;return n;
    }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* result) override {
        if(!result)return E_POINTER;
        *result=ProviderOptions_ServerSideProvider;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id,IUnknown** result) override {
        if(!result)return E_POINTER;
        *result=nullptr;
        if(id==UIA_InvokePatternId){*result=static_cast<IInvokeProvider*>(this);AddRef();}
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id,VARIANT* v) override {
        if(!v)return E_POINTER;
        VariantInit(v);
        auto s=root_->snapshot();
        const bool live=s.expanded && index_<s.count;
        switch(id) {
            case UIA_ControlTypePropertyId:
                v->vt=VT_I4;v->lVal=UIA_ButtonControlTypeId;return S_OK;
            case UIA_NamePropertyId:
                return setUiaName(v,live?s.actions[index_].name:L"Unavailable action");
            case UIA_AutomationIdPropertyId:
                return setUiaName(v,L"lynx.island.action."+std::to_wstring(index_));
            case UIA_IsControlElementPropertyId:
            case UIA_IsContentElementPropertyId:
            case UIA_IsKeyboardFocusablePropertyId:
                v->vt=VT_BOOL;v->boolVal=VARIANT_TRUE;return S_OK;
            case UIA_IsEnabledPropertyId:
                v->vt=VT_BOOL;v->boolVal=live?VARIANT_TRUE:VARIANT_FALSE;return S_OK;
            case UIA_IsOffscreenPropertyId:
                v->vt=VT_BOOL;v->boolVal=live?VARIANT_FALSE:VARIANT_TRUE;return S_OK;
            case UIA_HasKeyboardFocusPropertyId:
                v->vt=VT_BOOL;v->boolVal=live && s.focused==index_ ?
                    VARIANT_TRUE:VARIANT_FALSE;return S_OK;
            default:return S_OK;
        }
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
        IRawElementProviderSimple** result) override {
        if(!result)return E_POINTER;*result=nullptr;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                      IRawElementProviderFragment** result) override {
        if(!result)return E_POINTER;*result=nullptr;
        const auto s=root_->snapshot();
        if(direction==NavigateDirection_Parent) {
            *result=static_cast<IRawElementProviderFragment*>(root_);
            root_->AddRef();return S_OK;
        }
        int other=-1;
        if(direction==NavigateDirection_NextSibling && index_+1<s.count)other=index_+1;
        if(direction==NavigateDirection_PreviousSibling && index_>0)other=index_-1;
        return other>=0?root_->createChild(other,result):S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** result) override {
        if(!result)return E_POINTER;
        *result=SafeArrayCreateVector(VT_I4,0,2);
        if(!*result)return E_OUTOFMEMORY;
        LONG first=0,second=1;
        int tag=UiaAppendRuntimeId,number=index_+1;
        HRESULT hr=SafeArrayPutElement(*result,&first,&tag);
        if(SUCCEEDED(hr))hr=SafeArrayPutElement(*result,&second,&number);
        if(FAILED(hr)){SafeArrayDestroy(*result);*result=nullptr;}
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* r) override {
        if(!r)return E_POINTER;
        const auto s=root_->snapshot();
        if(!s.expanded || index_>=s.count) {
            *r=UiaRect{0,0,0,0};
            return S_OK;
        }
        const RECT rect=s.actions[index_].screen;
        *r=UiaRect{static_cast<double>(rect.left),static_cast<double>(rect.top),
                   static_cast<double>(rect.right-rect.left),
                   static_cast<double>(rect.bottom-rect.top)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** result) override {
        if(!result)return E_POINTER;*result=nullptr;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override {
        auto s=root_->snapshot();
        HWND hwnd=root_->hwnd();
        if(!s.expanded || index_>=s.count || !hwnd)return UIA_E_ELEMENTNOTAVAILABLE;
        return PostMessageW(hwnd,root_->focusMessage(),static_cast<WPARAM>(index_),0)
            ? S_OK:UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        if(!out)return E_POINTER;
        *out=static_cast<IRawElementProviderFragmentRoot*>(root_);
        root_->AddRef();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke() override {
        const auto s=root_->snapshot();
        HWND hwnd=root_->hwnd();
        if(!hwnd || !s.expanded || index_>=s.count)return UIA_E_ELEMENTNOTAVAILABLE;
        const auto kind=s.actions[index_].kind;
        // Window owner must revalidate current source capability before action.
        return PostMessageW(hwnd,root_->actionMessage(),static_cast<WPARAM>(kind),0)
            ? S_OK:UIA_E_ELEMENTNOTAVAILABLE;
    }
private:
    ~ShellUiaButton(){root_->Release();}
    std::atomic<ULONG> references_{1};
    ShellUiaRoot* root_;
    int index_=0;
};

inline HRESULT ShellUiaRoot::createChild(int index,IRawElementProviderFragment** out) {
    if(!out)return E_POINTER;*out=nullptr;
    const auto s=snapshot();
    if(!s.expanded || index<0 || index>=s.count)return S_OK;
    auto p=new(std::nothrow) ShellUiaButton(this,index);
    if(!p)return E_OUTOFMEMORY;
    *out=static_cast<IRawElementProviderFragment*>(p);
    return S_OK;
}

} // namespace lynx
