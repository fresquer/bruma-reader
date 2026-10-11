#pragma once
#include <array>
#include <string>
#include <vector>

#include "./FileBrowserActivity.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
struct ShortcutDefinition;
class BrumaHomeActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  int smallFontId = 0, bodyFontId = 0, titleFontId = 0;
  bool firstRenderDone = false;
  bool hasOpdsServers = false;
  std::vector<RecentBook> recentBooks;
  std::vector<const ShortcutDefinition*> dashboardShortcuts;
  std::vector<std::string> shortcutTitles;
  std::array<std::string, 3> bookTitles, bookAuthors;
  std::array<uint8_t, 3> progress{};
  std::array<bool, 3> completed{};
  std::string totalTime, lastSession, dailyGoal, streak, finished;
  const HomeMenuItem initialMenuItem;
  const bool cleanInitialRefresh;
  void reloadDashboard();
  int getMenuItemCount() const;
  int indexForMenuItem(HomeMenuItem item) const;
  void activateSelection();
  bool handleTouch();
  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onAppsOpen();
  void onReadingStatsOpen();
  void onSyncDayOpen();
  void onOpdsBrowserOpen();

 public:
  explicit BrumaHomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             HomeMenuItem initialMenuItemValue = HomeMenuItem::NONE, bool cleanInitialRefresh = false)
      : Activity("Home", renderer, mappedInput),
        initialMenuItem(initialMenuItemValue),
        cleanInitialRefresh(cleanInitialRefresh) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
