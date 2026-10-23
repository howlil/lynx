#pragma once
#include "activity_coordinator.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace lynx {
// Temporary reference shelf: does not copy, upload, index or persist file data.
class FileShelf {
public:
    static constexpr std::uint32_t kId=300;
    static constexpr std::size_t kLimit=5;
    bool add(const std::wstring& path) {
        if(path.empty() || path.size()>32767 || files_.size()>=kLimit)return false;
        if(std::find(files_.begin(),files_.end(),path)!=files_.end())return false;
        files_.push_back(path);
        return true;
    }
    bool remove(std::size_t index) {
        if(index>=files_.size())return false;
        files_.erase(files_.begin()+static_cast<std::ptrdiff_t>(index));
        return true;
    }
    void clear() { files_.clear(); }
    const std::vector<std::wstring>& files() const { return files_; }
    std::wstring firstPath() const { return files_.empty()?L"":files_.front(); }
    std::wstring firstName() const {
        if(files_.empty())return {};
        return std::filesystem::path(files_.front()).filename().wstring();
    }
    bool reconcile(ActivityCoordinator& coordinator) {
        if(files_.empty())return coordinator.end(kId,generation_,++revision_);
        const auto count=std::to_wstring(files_.size());
        return coordinator.upsert(Activity{kId,generation_,++revision_,15,
            L"File shelf · "+count, firstName(),true});
    }
private:
    std::vector<std::wstring> files_;
    std::uint64_t generation_=1,revision_=0;
};
} // namespace lynx
