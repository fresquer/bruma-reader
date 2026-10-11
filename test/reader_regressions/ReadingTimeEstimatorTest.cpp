#include <gtest/gtest.h>

#include <cstring>
#include <limits>

#include "util/ReadingTimeEstimator.h"

TEST(ReadingTimeEstimator, ProvisionalUntilFiveSamplesAndExponentialAverage) {
  ReadingTimeEstimator timer;
  ReadingPace pace{0, 0, 42};
  for (int page = 0; page < 5; ++page) {
    timer.pageVisible(page * 30000, page, 2);
    EXPECT_TRUE(timer.pageTurn(pace, page * 30000 + 10000, page + 1, 2, true));
    EXPECT_EQ(pace.effectivePageMs(), page < 4 ? 60000u : 10000u);
  }
  timer.pageVisible(200000, 5, 2);
  EXPECT_TRUE(timer.pageTurn(pace, 220000, 6, 2, true));
  EXPECT_EQ(pace.pageMs, 12000u);
}

TEST(ReadingTimeEstimator, RejectsQuickTurnsLongPausesJumpsReverseAndAutomatic) {
  ReadingTimeEstimator timer;
  ReadingPace pace{0, 0, 42};
  const auto rejects = [&](uint32_t elapsed, int page, int chapter, bool manual) {
    timer.pageVisible(100, 4, 2);
    EXPECT_FALSE(timer.pageTurn(pace, 100 + elapsed, page, chapter, manual));
    EXPECT_EQ(pace.samples, 0u);
  };
  rejects(4999, 5, 2, true);
  rejects(300001, 5, 2, true);
  rejects(10000, 6, 2, true);
  rejects(10000, 3, 2, true);
  rejects(10000, 0, 3, true);
  rejects(10000, 5, 2, false);
}

TEST(ReadingTimeEstimator, IntervalBoundsAreInclusiveAndCounterWrapIsSafe) {
  ReadingTimeEstimator timer;
  ReadingPace pace{0, 0, 42};
  timer.pageVisible(UINT32_MAX - 1000, 0, 0);
  EXPECT_TRUE(timer.pageTurn(pace, 3999, 1, 0, true));
  EXPECT_EQ(pace.pageMs, 5000u);
  timer.pageVisible(100000, 1, 0);
  EXPECT_TRUE(timer.pageTurn(pace, 400000, 2, 0, true));
}

TEST(ReadingTimeEstimator, LoadingMenuAndSleepRequireFreshVisiblePage) {
  ReadingTimeEstimator timer;
  ReadingPace pace{0, 0, 42};
  timer.pageVisible(1000, 0, 0);
  timer.interrupt();
  EXPECT_FALSE(timer.pageTurn(pace, 11000, 1, 0, true));
  timer.pageVisible(20000, 1, 0);
  EXPECT_TRUE(timer.pageTurn(pace, 30000, 2, 0, true));
  EXPECT_FALSE(timer.pageTurn(pace, 40000, 3, 0, true));  // still loading
  timer.pageVisible(50000, 3, 0);
  EXPECT_TRUE(timer.pageTurn(pace, 60000, 4, 0, true));
  EXPECT_EQ(pace.samples, 2u);
}

TEST(ReadingTimeEstimator, RepaintsDoNotResetVisibleTimeAndNewPageDoes) {
  ReadingTimeEstimator timer;
  ReadingPace pace{0, 0, 42};
  timer.pageVisible(1000, 0, 0);
  timer.pageVisible(8000, 0, 0);
  EXPECT_TRUE(timer.pageTurn(pace, 11000, 1, 0, true));
  EXPECT_EQ(pace.pageMs, 10000u);
  timer.pageVisible(20000, 1, 0);
  timer.pageVisible(29000, 2, 0);  // non-page-turn navigation
  EXPECT_FALSE(timer.pageTurn(pace, 30000, 3, 0, true));
}

TEST(ReadingTimeEstimator, LayoutInvalidationAndOldOrCorruptProfiles) {
  ReadingPace old;
  old.validate();
  EXPECT_EQ(old.effectivePageMs(), 60000u);
  ReadingPace pace{12000, 9, 42};
  EXPECT_FALSE(pace.prepare(42));
  EXPECT_EQ(pace.samples, 9u);
  EXPECT_TRUE(pace.prepare(43));
  EXPECT_EQ(pace.samples, 0u);
  EXPECT_EQ(pace.layout, 43u);
  for (auto ms : {0u, 4999u, 300001u, UINT32_MAX}) {
    pace = {ms, 8, 43};
    pace.validate();
    EXPECT_EQ(pace.samples, 0u);
  }
}

TEST(ReadingTimeEstimator, EstimatesBookFromCurrentChapterWithoutScanningOthers) {
  EXPECT_DOUBLE_EQ(ReadingTimeEstimator::epubBookPages(5, 10, .25, .5), 25);
  EXPECT_DOUBLE_EQ(ReadingTimeEstimator::epubBookPages(10, 10, .5, 1), 0);
  EXPECT_DOUBLE_EQ(ReadingTimeEstimator::remainingPages(5, 10), 5);
  EXPECT_DOUBLE_EQ(ReadingTimeEstimator::remainingPages(10, 10), 0);
  EXPECT_DOUBLE_EQ(ReadingTimeEstimator::remainingPages(11, 10), 0);
  EXPECT_LT(ReadingTimeEstimator::remainingPages(1, 0), 0);
  EXPECT_LT(ReadingTimeEstimator::epubBookPages(1, 10, .5, .5), 0);
  EXPECT_LT(ReadingTimeEstimator::epubBookPages(1, 10, 1, .5), 0);
  EXPECT_LT(ReadingTimeEstimator::epubBookPages(1, 10, 0, std::numeric_limits<double>::quiet_NaN()), 0);
}

TEST(ReadingTimeEstimator, FormatsRoundedMinutesHoursFinishAndUnavailableWithinBuffer) {
  char text[48];
  const auto format = [&](double pages, uint32_t pageMs) {
    ReadingTimeEstimator::format(text, sizeof(text), pages, pageMs, "min", "h", "—");
    return std::string(text);
  };
  EXPECT_EQ(format(24.1, 60000), "~25 min");
  EXPECT_EQ(format(80, 60000), "~1 h 20 min");
  EXPECT_EQ(format(.01, 5000), "~1 min");
  EXPECT_EQ(format(0, 60000), "0 min");
  EXPECT_EQ(format(-1, 60000), "—");
  EXPECT_EQ(format(std::numeric_limits<double>::infinity(), 60000), "—");
  EXPECT_EQ(format(1e30, 60000), "—");
  char tiny[4] = {};
  ReadingTimeEstimator::format(tiny, sizeof(tiny), 80, 60000, "min", "h", "—");
  EXPECT_EQ(tiny[3], '\0');
}
