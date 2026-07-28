#pragma once

#include <string>
#include <vector>

namespace olc {

// Lists and navigates captured photos (.jpg/.jpeg/.png) in the output
// directory. Files are timestamp-named, so a plain name sort is chronological;
// newest is shown first.
class Gallery {
public:
    explicit Gallery(std::string dir) : dir_(std::move(dir)) { refresh(); }

    // Re-scan the directory, keeping the selection near where it was.
    void refresh();

    bool empty() const { return files_.empty(); }
    int count() const { return static_cast<int>(files_.size()); }
    int index() const { return index_; }

    const std::string& current() const { return files_[index_]; }

    void next();
    void prev();

    // Delete the current file from disk and re-scan. No-op when empty.
    void deleteCurrent();

private:
    std::string dir_;
    std::vector<std::string> files_; // full paths, newest first
    int index_ = 0;
};

} // namespace olc
