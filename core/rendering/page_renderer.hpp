#pragma once
#include "core/layout/layout.hpp"
#include <include/core/SkCanvas.h>
#include <include/core/SkPicture.h>
#include <memory>
namespace wf {
class PageRenderer {
  public:
    PageRenderer();
    ~PageRenderer();
    std::size_t synchronize(const Layout &);
    void drawPage(SkCanvas &, std::size_t page) const;
    void drawSelection(SkCanvas &, const Layout &, Selection, std::size_t page) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wf
