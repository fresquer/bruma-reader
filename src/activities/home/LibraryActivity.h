#pragma once
#include <I18n.h>

#include "LibraryStore.h"
#include "activities/UiListActivity.h"

class LibraryActivity final : public UiListActivity {
 public:
  LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool restoreSelection = false);
  void onEnter() override;
  void restoreAfterFailedOpen() {
    restore = true;
    onEnter();
  }
  void onExit() override;
  bool preventAutoSleep() override { return scanning; }
  bool handleHomeGesture() override;

 private:
  static constexpr int WINDOW = 24;
  LibraryStore store;
  LibraryStore::Book books[WINDOW];
  std::string subtitles[WINDOW];
  freeink::ui::ListItem rows[WINDOW];
  freeink::ui::ListItem menuRows[6];
  std::string menuBookPath, menuBookTitle;
  bool menuBookFinished = false;
  char heading[96] = {};
  int windowStart = -1, windowCount = 0;
  bool menu = false, scanning = false, restore;
  StrId notice = StrId::STR_NONE_OPT;
  unsigned long lastProgress = 0;
  char progress[96] = {};
  freeink::ui::ListNav bookNav;
  static std::string savedPath;
  static int savedIndex;
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int) override { showMenu(); }
  bool handleCustomInput() override;
  bool handleButtons() override;
  const char* headerTitle() const override { return heading[0] ? heading : tr(STR_LIBRARY); }
  void drawFooter() override;
  void drawChrome() override;
  void navigateButtons() override;
  void cycleOrder(int delta);
  int orderBandHeight() const;
  void refreshWindow(int start, int visible);
  void clearWindow();
  void showMenu();
  bool refreshReadingView();
  void refreshEmptyNotice();
  bool menuAvailable() const;
  void beginUpdate();
  void finishUpdate(LibraryStore::ScanState state);
};
