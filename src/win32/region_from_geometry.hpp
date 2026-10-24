#pragma once
// Convert a filled Direct2D path into a native HWND clipping/input region.
// SetWindowRgn is managed by USER32 after successful transfer. Regions exclude
// pixels outside the outline from both drawing and cross-process hit-testing.
#include <windows.h>
#include <d2d1.h>
#include <atomic>
#include <cmath>
#include <new>
#include <vector>

namespace lynx {

class D2DRegionSink final : public ID2D1SimplifiedGeometrySink {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(id==__uuidof(IUnknown) || id==__uuidof(ID2D1SimplifiedGeometrySink))
            *out=static_cast<ID2D1SimplifiedGeometrySink*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override {
        const auto n=--refs_;if(!n)delete this;return n;
    }
    void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE) override {}
    void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) override {}
    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F start,D2D1_FIGURE_BEGIN) override {
        figures_.emplace_back();
        append(start);
    }
    void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* points,UINT count) override {
        for(UINT i=0;i<count;i++)append(points[i]);
    }
    void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT* segments,UINT count) override {
        // Simplify(...LINES...) normally removes cubic segments. Fallback
        // keeps the contour closed if a geometry implementation emits them.
        for(UINT i=0;i<count;i++)append(segments[i].point3);
    }
    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END) override {}
    HRESULT STDMETHODCALLTYPE Close() override {return S_OK;}

    HRGN createRegion() const {
        HRGN region=CreateRectRgn(0,0,0,0);
        if(!region)return nullptr;
        for(const auto& polygon:figures_) {
            if(polygon.size()<3)continue;
            HRGN part=CreatePolygonRgn(polygon.data(),
                static_cast<int>(polygon.size()),WINDING);
            if(!part) { DeleteObject(region);return nullptr; }
            const int combined=CombineRgn(region,region,part,RGN_OR);
            DeleteObject(part);
            if(combined==ERROR) { DeleteObject(region);return nullptr; }
        }
        return region;
    }
private:
    ~D2DRegionSink()=default;
    void append(D2D1_POINT_2F p) {
        if(figures_.empty())return;
        const POINT pos{static_cast<LONG>(std::lround(p.x)),
                        static_cast<LONG>(std::lround(p.y))};
        auto& points=figures_.back();
        if(points.empty() || points.back().x!=pos.x || points.back().y!=pos.y)
            points.push_back(pos);
    }
    std::atomic<ULONG> refs_{1};
    std::vector<std::vector<POINT>> figures_;
};

inline bool applyGeometryWindowRegion(HWND hwnd,ID2D1Geometry* geometry,
                                       float dpi) {
    if(!hwnd || !geometry || dpi<=0.f)return false;
    auto* sink=new(std::nothrow) D2DRegionSink();
    if(!sink)return false;
    const D2D1_MATRIX_3X2_F transform{dpi,0.f,0.f,dpi,0.f,0.f};
    const HRESULT result=geometry->Simplify(
        D2D1_GEOMETRY_SIMPLIFICATION_OPTION_LINES,&transform,0.5f,sink);
    HRGN region=SUCCEEDED(result)?sink->createRegion():nullptr;
    sink->Release();
    if(!region)return false;
    if(!SetWindowRgn(hwnd,region,TRUE)) {
        DeleteObject(region); // failure: ownership never transferred
        return false;
    }
    return true;
}

} // namespace lynx
