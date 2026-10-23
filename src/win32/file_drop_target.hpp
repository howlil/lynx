#pragma once
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
namespace lynx {
// Accepts CF_HDROP only. Stores file paths, not file bytes.
// RegisterDragDrop requires OleInitialize on the window/UI thread.
class FileDropTarget final : public IDropTarget {
public:
    using Handler=std::function<bool(std::vector<std::wstring>)>;
    explicit FileDropTarget(Handler onDrop):onDrop_(std::move(onDrop)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(iid==__uuidof(IUnknown)||iid==__uuidof(IDropTarget))
            *out=static_cast<IDropTarget*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override {
        auto count=--refs_;
        if(!count)delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data,DWORD,POINTL,DWORD* effect) override {
        if(!effect)return E_POINTER;
        accepted_=supportsFiles(data);
        *effect=(accepted_ && (*effect & DROPEFFECT_COPY))?DROPEFFECT_COPY:DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD,POINTL,DWORD* effect) override {
        if(!effect)return E_POINTER;
        *effect=(accepted_ && (*effect & DROPEFFECT_COPY))?DROPEFFECT_COPY:DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {accepted_=false;return S_OK;}
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data,DWORD,POINTL,DWORD* effect) override {
        if(!effect)return E_POINTER;
        const bool shouldCopy=accepted_ && (*effect & DROPEFFECT_COPY);
        accepted_=false;
        *effect=DROPEFFECT_NONE;
        if(!shouldCopy || !data)return S_OK;
        FORMATETC fmt{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
        STGMEDIUM medium{};
        if(FAILED(data->GetData(&fmt,&medium)))return S_OK;
        std::vector<std::wstring> paths;
        auto hdrop=reinterpret_cast<HDROP>(medium.hGlobal);
        const UINT count=DragQueryFileW(hdrop,0xFFFFFFFF,nullptr,0);
        for(UINT i=0;i<std::min<UINT>(count,5);++i) {
            const UINT chars=DragQueryFileW(hdrop,i,nullptr,0);
            if(chars==0 || chars>32767)continue;
            std::wstring path(chars+1,L'\0');
            if(DragQueryFileW(hdrop,i,path.data(),chars+1)>0) {
                path.resize(chars);
                paths.push_back(std::move(path));
            }
        }
        ReleaseStgMedium(&medium);
        if(!paths.empty() && onDrop_(std::move(paths))) {
            *effect=DROPEFFECT_COPY; // source data never deleted
        }
        return S_OK;
    }
private:
    ~FileDropTarget()=default;
    static bool supportsFiles(IDataObject* data) {
        if(!data)return false;
        FORMATETC fmt{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
        return SUCCEEDED(data->QueryGetData(&fmt));
    }
    std::atomic<ULONG> refs_{1};
    bool accepted_=false; // called on UI STA during an OLE drag loop
    Handler onDrop_;
};
} // namespace lynx
