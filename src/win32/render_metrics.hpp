#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

namespace lynx {

// Opt-in benchmark only: set LYNX_RENDER_METRICS=1 before launching.
// QPC covers preparation + CPU drawing + synchronous UpdateLayeredWindow.
// This is NOT GPU display latency, actual FPS or an apples-to-apples
// DirectComposition/Visual Layer comparison.
class RenderMetrics {
public:
    RenderMetrics() {
        wchar_t enabled[4]{};
        enabled_ = GetEnvironmentVariableW(L"LYNX_RENDER_METRICS",enabled,4)==1 &&
                   enabled[0]==L'1';
        QueryPerformanceFrequency(&frequency_);
    }
    bool enabled() const {return enabled_ && frequency_.QuadPart>0;}
    LARGE_INTEGER begin() const {
        LARGE_INTEGER q{};
        if(enabled())QueryPerformanceCounter(&q);
        return q;
    }
    void finish(LARGE_INTEGER start, bool ok) {
        if(!enabled())return;
        LARGE_INTEGER end{};
        QueryPerformanceCounter(&end);
        const double milliseconds =
            1000.0*static_cast<double>(end.QuadPart-start.QuadPart)/
            static_cast<double>(frequency_.QuadPart);
        if(!std::isfinite(milliseconds) || milliseconds<0)return;
        ++total_;
        if(!ok)++failed_;
        accumulatedMs_+=milliseconds;
        maxMs_=std::max(maxMs_,milliseconds);
        samples_[next_] = milliseconds;
        next_=(next_+1)%samples_.size();
        count_=std::min(count_+1,samples_.size());
    }
    void exportCsv() const {
        if(!enabled())return;
        wchar_t dir[MAX_PATH]{};
        DWORD len=GetTempPathW(MAX_PATH,dir);
        if(!len || len>=MAX_PATH)return;
        const std::wstring path=std::wstring(dir)+L"lynx-render-metrics.csv";
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,
                                nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return;
        std::vector<double> ordered(samples_.begin(),samples_.begin()+count_);
        std::sort(ordered.begin(),ordered.end());
        const double p95=count_ ? ordered[static_cast<std::size_t>(
            std::ceil(count_*.95)-1)] : 0;
        char line[512]{};
        const int written=std::snprintf(line,sizeof(line),
            "renderer,total_draw_calls,failed_draw_calls,mean_cpu_ms,p95_recent_cpu_ms,max_cpu_ms,recent_window\n"
            "layered_gdiplus,%llu,%llu,%.4f,%.4f,%.4f,%llu\n",
            static_cast<unsigned long long>(total_),
            static_cast<unsigned long long>(failed_),
            total_ ? accumulatedMs_/static_cast<double>(total_) : 0.0,
            p95,maxMs_,static_cast<unsigned long long>(count_));
        if(written>0 && written<static_cast<int>(sizeof(line))) {
            DWORD ignored=0;
            WriteFile(file,line,static_cast<DWORD>(written),&ignored,nullptr);
        }
        CloseHandle(file);
    }
private:
    bool enabled_=false;
    LARGE_INTEGER frequency_{};
    std::array<double,2048> samples_{};
    std::size_t next_=0,count_=0;
    std::uint64_t total_=0,failed_=0;
    double accumulatedMs_=0,maxMs_=0;
};

struct ScopedRenderSample {
    RenderMetrics& recorder;
    LARGE_INTEGER started;
    bool ok=false;
    explicit ScopedRenderSample(RenderMetrics& metrics)
        : recorder(metrics),started(metrics.begin()){}
    ~ScopedRenderSample(){recorder.finish(started,ok);}
    void success(){ok=true;}
};
} // namespace lynx
