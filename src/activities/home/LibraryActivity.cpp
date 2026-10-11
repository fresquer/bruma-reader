#include "LibraryActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <MemoryBudget.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;
using enum StrId;
std::string LibraryActivity::savedPath;
int LibraryActivity::savedIndex = 0;

LibraryActivity::LibraryActivity(GfxRenderer& renderer, MappedInputManager& input, bool restoreSelection)
    : UiListActivity("Library", renderer, input, true), restore(restoreSelection) {}

void LibraryActivity::onEnter() {
  UiListActivity::onEnter();
  const auto order = static_cast<LibraryStore::Order>(SETTINGS.libraryOrder);
  if (!store.load(order)) {
    if (Storage.exists("/.crosspoint/library.bin") || Storage.exists("/.crosspoint/library.bin.bak"))
      notice = STR_LIBRARY_FAILED;
  } else if (restore) {
    const int index = store.findPath(savedPath);
    nav.selected = index >= 0 ? index : std::min(savedIndex, std::max(0, static_cast<int>(store.count()) - 1));
  }
  LOG_DBG("LIB", "Catalog: %u books, %u index bytes", store.count(), static_cast<unsigned>(store.indexBytes()));
  if (!store.exists() || !store.count()) notice = notice == STR_NONE_OPT ? STR_LIBRARY_EMPTY : notice;
  requestUpdate();
}
void LibraryActivity::clearWindow() {
  for (int i = 0; i < WINDOW; ++i) {
    rows[i] = fui::ListItem{};
    books[i] = LibraryStore::Book{};
    subtitles[i].clear();
  }
  windowStart = -1;
  windowCount = 0;
}
void LibraryActivity::onExit() {
  Activity::onExit();
  clearWindow();
  store.release();
}
bool LibraryActivity::handleHomeGesture() {
  if (scanning) {
    RenderLock lock(*this);
    store.cancel();
    scanning = false;
  }
  return false;
}
int LibraryActivity::listCount() const {
  if (scanning) return 0;
  if (menu) return 5;
  if (!store.exists() || !store.count() || notice == STR_LIBRARY_MISSING || notice == STR_LIBRARY_FAILED) return 1;
  return static_cast<int>(store.count());
}
void LibraryActivity::refreshWindow(int start, int visible) {
  const int total = static_cast<int>(store.count());
  const int count = std::min(WINDOW, std::max(1, visible + 1));
  start = std::max(0, std::min(start, std::max(0, total - count)));
  if (windowStart == start && windowCount == std::min(count, total - start)) return;
  clearWindow();
  windowCount = std::min(count, total - start);
  for (int i = 0; i < windowCount; ++i) {
    if (!store.readBook(start + i, books[i])) {
      notice = STR_LIBRARY_FAILED;
      windowCount = 0;
      return;
    }
    subtitles[i] = books[i].author;
    if (books[i].duplicate) {
      const std::string folder = books[i].path.substr(0, books[i].path.find_last_of('/'));
      if (!subtitles[i].empty()) subtitles[i] += " · ";
      subtitles[i] += folder.empty() ? "/" : folder;
    }
    rows[i].label = books[i].title.c_str();
    rows[i].subtitle = subtitles[i].empty() ? nullptr : subtitles[i].c_str();
    rows[i].icon = listIconFor(UITheme::getFileIcon(books[i].path), 32);
    rows[i].actionValue = static_cast<int16_t>(start + i);
  }
  windowStart = start;
  renderer.prewarmFallbackText(
      uiScaleSpec().smallFontId,
      [](const void* ctx, uint32_t i) -> const char* {
        return static_cast<const LibraryActivity*>(ctx)->books[i].title.c_str();
      },
      this, windowCount, EpdFontFamily::BOLD);
  renderer.prewarmFallbackText(
      uiScaleSpec().smallFontId,
      [](const void* ctx, uint32_t i) -> const char* {
        return static_cast<const LibraryActivity*>(ctx)->subtitles[i].c_str();
      },
      this, windowCount);
}
void LibraryActivity::showMenu() {
  if (menu || scanning) return;
  {
    RenderLock lock(*this);
    bookNav = nav;
    menu = true;
    nav.reset(SETTINGS.libraryOrder);
    closeRouting();
  }
  requestUpdate();
}
void LibraryActivity::beginUpdate() {
  {
    RenderLock lock(*this);
    menu = false;
    scanning = store.beginScan();
    nav.reset();
    closeRouting();
    clearWindow();
    notice = scanning ? STR_NONE_OPT : STR_LIBRARY_FAILED;
    std::snprintf(progress, sizeof(progress), "%s: 0", I18N.get(STR_LIBRARY_FOUND));
  }
  lastProgress = millis();
  requestUpdateAndWait();
}
void LibraryActivity::finishUpdate(LibraryStore::ScanState state) {
  scanning = false;
  nav.reset();
  clearWindow();
  closeRouting();
  notice = state == LibraryStore::ScanState::Failed ? STR_LIBRARY_FAILED
           : !store.count()                         ? STR_LIBRARY_EMPTY
                                                    : STR_NONE_OPT;
  if (state == LibraryStore::ScanState::Failed) store.load(static_cast<LibraryStore::Order>(SETTINGS.libraryOrder));
  LOG_DBG("LIB", "Scan state %u, %u books, %u index bytes, free heap %u", static_cast<unsigned>(state), store.count(),
          static_cast<unsigned>(store.indexBytes()), MemoryBudget::snapshot().freeHeap);
}
bool LibraryActivity::handleCustomInput() {
  if (!scanning) return false;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    {
      RenderLock lock(*this);
      store.cancel();
      finishUpdate(LibraryStore::ScanState::Cancelled);
    }
    requestUpdate();
    return true;
  }
  // Paint the publishing phase before its bounded validation/sort operation.
  if (store.scanState() == LibraryStore::ScanState::Publishing) {
    {
      RenderLock lock(*this);
      std::snprintf(progress, sizeof(progress), "%s", I18N.get(STR_LIBRARY_SAVING));
    }
    requestUpdateAndWait();
  }
  bool finished = false;
  {
    RenderLock lock(*this);
    const auto state = store.step();
    if (state == LibraryStore::ScanState::Complete || state == LibraryStore::ScanState::Failed) {
      finishUpdate(state);
      finished = true;
    } else if (millis() - lastProgress >= 750) {
      std::snprintf(progress, sizeof(progress), "%s: %lu", I18N.get(STR_LIBRARY_FOUND),
                    static_cast<unsigned long>(store.found()));
      lastProgress = millis();
      finished = true;
    }
  }
  if (finished) requestUpdate();
  return true;
}
bool LibraryActivity::handleButtons() {
  if (!menu && store.count() && notice == STR_NONE_OPT &&
      mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 1000)) {
    showMenu();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (menu) {
      {
        RenderLock lock(*this);
        menu = false;
        nav = bookNav;
        closeRouting();
      }
      requestUpdate();
    } else if (notice != STR_NONE_OPT && store.exists() && store.count()) {
      {
        RenderLock lock(*this);
        notice = STR_NONE_OPT;
        nav.reset();
        closeRouting();
      }
      requestUpdate();
    } else
      onGoHome();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!menu && store.count() && notice == STR_NONE_OPT && mappedInput.getHeldTime() >= 1000)
      showMenu();
    else
      activateIndex(nav.selected);
    return true;
  }
  return false;
}
void LibraryActivity::activateIndex(int index) {
  if (scanning || index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  if (menu) {
    if (index == 4) {
      beginUpdate();
      return;
    }
    {
      RenderLock lock(*this);
      const auto order = static_cast<LibraryStore::Order>(index);
      if (!store.setOrder(order)) {
        store.load(static_cast<LibraryStore::Order>(SETTINGS.libraryOrder));
        notice = STR_LIBRARY_FAILED;
      } else {
        SETTINGS.libraryOrder = index;
        SETTINGS.saveToFile();
        notice = store.count() ? STR_NONE_OPT : STR_LIBRARY_EMPTY;
      }
      menu = false;
      nav.reset();
      clearWindow();
      closeRouting();
    }
    requestUpdate();
    return;
  }
  if (!store.exists() || !store.count() || notice != STR_NONE_OPT) {
    beginUpdate();
    return;
  }
  std::string path;
  {
    RenderLock lock(*this);
    LibraryStore::Book book;
    if (!store.readBook(index, book))
      notice = STR_LIBRARY_FAILED;
    else if (!Storage.exists(book.path.c_str()))
      notice = STR_LIBRARY_MISSING;
    else {
      path = book.path;
      savedPath = path;
      savedIndex = index;
    }
    if (!path.empty()) {
      closeRouting();
      clearWindow();
      store.release();
    } else {
      nav.reset();
      closeRouting();
    }
  }
  if (path.empty())
    requestUpdate();
  else
    onSelectBook(path);
}
void LibraryActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (scanning) {
    screen.centeredText(progress, screen.theme().bodyText);
    return;
  }
  fui::ListProps props;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  if (menu) {
    const StrId labels[5] = {STR_TITLE, STR_AUTHOR, STR_LIBRARY_LAST_READ, STR_LIBRARY_ADDED, STR_LIBRARY_UPDATE};
    for (int i = 0; i < 5; ++i) {
      menuRows[i] = fui::ListItem{};
      menuRows[i].label = I18N.get(labels[i]);
      menuRows[i].actionValue = i;
      if (i == SETTINGS.libraryOrder) menuRows[i].value = I18N.get(STR_SELECTED);
    }
    props.items = menuRows;
    syncListViewport(screen, props);
    screen.list(props);
    return;
  }
  if (!store.exists() || !store.count() || notice != STR_NONE_OPT) {
    if (notice != STR_NONE_OPT) screen.centeredText(I18N.get(notice), screen.theme().smallText);
    menuRows[0] = fui::ListItem{};
    menuRows[0].label = I18N.get(store.exists() ? STR_LIBRARY_UPDATE : STR_LIBRARY_CREATE);
    menuRows[0].actionValue = 0;
    props.count = 1;
    props.items = menuRows;
    syncListViewport(screen, props);
    screen.list(props);
    return;
  }
  props.labelText = screen.theme().smallText;
  props.labelText.bold = true;
  syncListViewport(screen, props, true);
  refreshWindow(nav.top, nav.visibleRows);
  props.items = rows;
  props.itemsWindowFirst = static_cast<uint16_t>(windowStart < 0 ? 0 : windowStart);
  props.itemsWindowCount = static_cast<uint16_t>(windowCount);
  if (windowCount)
    screen.list(props);
  else if (notice != STR_NONE_OPT) {
    screen.centeredText(I18N.get(notice), screen.theme().smallText);
    menuRows[0] = fui::ListItem{};
    menuRows[0].label = tr(STR_LIBRARY_UPDATE);
    props.items = menuRows;
    props.count = 1;
    props.itemsWindowFirst = props.itemsWindowCount = 0;
    syncListViewport(screen, props);
    screen.list(props);
  }
}
void LibraryActivity::drawFooter() {
  const auto back = scanning ? STR_CANCEL : menu || (notice != STR_NONE_OPT && store.count()) ? STR_BACK : STR_HOME;
  const auto labels =
      mappedInput.mapLabels(I18N.get(back),
                            scanning ? ""
                                     : I18N.get(menu                                      ? STR_SELECT
                                                : store.count() && notice == STR_NONE_OPT ? STR_LIBRARY_OPEN_MENU
                                                                                          : STR_SELECT),
                            scanning ? "" : tr(STR_DIR_UP), scanning ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
