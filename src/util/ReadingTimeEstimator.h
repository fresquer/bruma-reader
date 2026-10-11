#pragma once

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

// Persisted per book. Layout identifies the pagination the samples belong to.
struct ReadingPace {
  uint32_t pageMs = 0;
  uint32_t samples = 0;
  uint32_t layout = 0;

  void validate() {
    if (!layout || !samples || pageMs < 5000 || pageMs > 300000) {
      pageMs = samples = 0;
    }
  }
  bool prepare(uint32_t key) {
    if (layout == key) return false;
    *this = {0, 0, key};
    return true;
  }
  uint32_t effectivePageMs() const { return samples >= 5 ? pageMs : 60000; }
};

// Allocation-free timing of a fully displayed page. Loading/menu time is never
// credited: every transition disarms the timer until pageVisible() is called.
class ReadingTimeEstimator {
  uint32_t visibleAt = 0;
  int32_t page = -1;
  int32_t chapter = -1;
  std::atomic<bool> armed{false};

 public:
  void interrupt() { armed = false; }
  void pageVisible(uint32_t now, int32_t newPage, int32_t newChapter) {
    if (armed && page == newPage && chapter == newChapter) return;
    page = newPage;
    chapter = newChapter;
    visibleAt = now;
    armed = true;
  }
  bool pageTurn(ReadingPace& pace, uint32_t now, int32_t nextPage, int32_t nextChapter, bool manualSingle) {
    const uint32_t elapsed = now - visibleAt;  // uint32 wrap is intentional
    const bool valid =
        armed && manualSingle && nextChapter == chapter && nextPage == page + 1 && elapsed >= 5000 && elapsed <= 300000;
    interrupt();
    if (!valid) return false;
    pace.pageMs = pace.samples ? static_cast<uint32_t>((uint64_t(pace.pageMs) * 4 + elapsed + 2) / 5) : elapsed;
    if (pace.samples != std::numeric_limits<uint32_t>::max()) ++pace.samples;
    return true;
  }

  static double remainingPages(double displayedPage, double totalPages) {
    if (!std::isfinite(displayedPage) || !std::isfinite(totalPages) || totalPages <= 0 || displayedPage < 0) return -1;
    return displayedPage >= totalPages ? 0 : totalPages - displayedPage;
  }
  static double epubBookPages(double displayedPage, double chapterPages, double startProgress, double endProgress) {
    if (remainingPages(displayedPage, chapterPages) < 0 || !std::isfinite(startProgress) ||
        !std::isfinite(endProgress) || startProgress < 0 || endProgress > 1 || endProgress <= startProgress)
      return -1;
    const double fraction = std::fmin(1.0, displayedPage / chapterPages);
    const double progress = startProgress + fraction * (endProgress - startProgress);
    return std::fmax(0.0, (1.0 - progress) * chapterPages / (endProgress - startProgress));
  }
  static void format(char* out, size_t capacity, double pages, uint32_t pageMs, const char* minutes, const char* hours,
                     const char* unavailable) {
    if (!capacity) return;
    if (!std::isfinite(pages) || pages < 0 || !pageMs) {
      std::snprintf(out, capacity, "%s", unavailable);
      return;
    }
    const double rounded = std::ceil(pages * pageMs / 60000.0);
    if (!std::isfinite(rounded) || rounded > std::numeric_limits<uint32_t>::max()) {
      std::snprintf(out, capacity, "%s", unavailable);
      return;
    }
    const auto mins = static_cast<uint32_t>(rounded);
    if (!mins)
      std::snprintf(out, capacity, "0 %s", minutes);
    else if (mins < 60)
      std::snprintf(out, capacity, "~%lu %s", static_cast<unsigned long>(mins), minutes);
    else
      std::snprintf(out, capacity, "~%lu %s %lu %s", static_cast<unsigned long>(mins / 60), hours,
                    static_cast<unsigned long>(mins % 60), minutes);
  }
};
