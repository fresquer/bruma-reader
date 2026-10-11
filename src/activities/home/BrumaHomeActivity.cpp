#include "BrumaHomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FreeInkUIGfxRenderer.h>
#include <FreeInkUIIcon.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Txt.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FavoritesStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "activities/apps/AchievementsActivity.h"
#include "activities/apps/BookmarksAppActivity.h"
#include "activities/apps/DictionaryActivity.h"
#include "activities/apps/FavoritesAppActivity.h"
#include "activities/apps/FlashcardsAppActivity.h"
#include "activities/apps/IfFoundActivity.h"
#include "activities/apps/ReadingHeatmapActivity.h"
#include "activities/apps/ReadingProfileActivity.h"
#include "activities/apps/ReadingStatsActivity.h"
#include "activities/apps/ScreenCleanActivity.h"
#include "activities/apps/SleepAppActivity.h"
#include "activities/apps/SyncDayActivity.h"
#include "activities/settings/ClockSyncActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/icons/listIcons.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/ShortcutRegistry.h"
#include "util/ShortcutUiMetadata.h"
#include "util/TimeUtils.h"

namespace {
constexpr int HOME_BOOK_LIMIT = 3;
constexpr int HOME_ACTION_COUNT = 4;  // Statistics, Library, Apps, clock sync.

const ReadingBookStats* bookStats(const RecentBook& book) {
  const auto* stats = book.bookId.empty() ? nullptr : READING_STATS.findBook(book.bookId);
  return stats ? stats : READING_STATS.findBook(book.path);
}

// All bands share this geometry for rendering and touch hit testing.
struct HomeLayout {
  int x, width, top, currentWidth, recentX, recentWidth, recentTop, rowHeight;
  int statsTop, metricsTop, detailsTop, navTop, clockTop, footer;
  bool landscape;
  explicit HomeLayout(const GfxRenderer& renderer) {
    const auto& m = UITheme::getInstance().getMetrics();
    x = std::max(20, m.contentSidePadding);
    width = renderer.getScreenWidth() - 2 * x;
    top = m.topPadding + 40;
    footer = renderer.getScreenHeight() - m.buttonHintsHeight - 8;
    clockTop = footer - 42;
    navTop = clockTop - 50;
    detailsTop = navTop - 52;
    metricsTop = detailsTop - 88;
    statsTop = metricsTop - 38;
    landscape = renderer.getScreenWidth() > renderer.getScreenHeight();
    if (landscape) {
      metricsTop = detailsTop - 66;
      statsTop = metricsTop - 28;
    }
    rowHeight = landscape ? 60 : 88;
    recentTop = landscape ? top : statsTop - 2 * rowHeight - 52;
    currentWidth = landscape ? (width - 28) / 2 : width;
    recentX = landscape ? x + currentWidth + 28 : x;
    recentWidth = landscape ? width - currentWidth - 28 : width;
  }
};

const freeink::Icon* homeIcon(UIIcon type) {
  switch (type) {
    case UIIcon::Library:
      return &icon_library_24;
    case UIIcon::Book:
      return &icon_book_24;
    case UIIcon::Apps:
    case UIIcon::Settings:
      return &icon_settings_2_24;
    case UIIcon::Recent:
      return &icon_history_24;
    case UIIcon::Folder:
      return &icon_folder_24;
    case UIIcon::Text:
      return &icon_file_text_24;
    case UIIcon::Image:
      return &icon_image_24;
    case UIIcon::File:
      return &icon_file_24;
    case UIIcon::Wifi:
      return &icon_wifi_24;
    case UIIcon::Hotspot:
      return &icon_radio_tower_24;
    case UIIcon::Usb:
      return &icon_usb_24;
    case UIIcon::Bookmark:
      return &icon_bookmark_24;
    case UIIcon::Trophy:
      return &icon_trophy_24;
    case UIIcon::Heart:
      return &icon_heart_24;
    case UIIcon::Transfer:
      return &icon_arrow_right_left_24;
    default:
      return nullptr;
  }
}

// Align actual glyph ink, including accents and descenders, rather than the
// font's larger line box. Icon artwork supplies its own optical center.
int textInkCenter(const GfxRenderer& renderer, int fontId, const char* label) {
  const auto& fonts = renderer.getFontMap();
  const auto font = fonts.find(fontId);
  if (font == fonts.end()) return renderer.getLineHeight(fontId) / 2;
  int top = INT_MAX, bottom = INT_MIN;
  const int ascent = renderer.getFontAscenderSize(fontId);
  const auto* cursor = reinterpret_cast<const unsigned char*>(label);
  while (*cursor) {
    const uint32_t cp = utf8NextCodepoint(&cursor);
    const auto* glyph = font->second.getGlyph(cp, EpdFontFamily::BOLD);
    if (!glyph || !glyph->height) continue;
    top = std::min(top, ascent - glyph->top);
    bottom = std::max(bottom, ascent - glyph->top + glyph->height);
  }
  return top == INT_MAX ? ascent / 2 : (top + bottom) / 2;
}

void drawProgress(const GfxRenderer& renderer, int x, int y, int width, int percent, bool black = true) {
  renderer.drawRect(x, y, width, 12, black);
  const int fill = (width - 4) * std::clamp(percent, 0, 100) / 100;
  if (fill > 0) renderer.fillRect(x + 2, y + 2, fill, 8, black);
}
}  // namespace

void BrumaHomeActivity::reloadDashboard() {
  // Three books bound both memory and the work per home refresh. Read history
  // supplements Recents because finished EPUBs may be removed from that list.
  recentBooks.clear();
  recentBooks.reserve(HOME_BOOK_LIMIT);
  const auto append = [this](const RecentBook& book) {
    if (book.path.empty() || RecentBooksStore::isMissing(book)) return;
    for (const auto& existing : recentBooks)
      if (existing == book) return;
    if (recentBooks.size() < HOME_BOOK_LIMIT) recentBooks.push_back(book);
  };
  const auto& recents = RECENT_BOOKS.getBooks();
  if (!APP_STATE.openEpubPath.empty() && Storage.exists(APP_STATE.openEpubPath.c_str())) {
    const auto* stats = READING_STATS.findBook(APP_STATE.openEpubPath);
    if (stats)
      append(RecentBook{stats->bookId, stats->path, stats->title, stats->author, stats->coverBmpPath});
    else
      for (const auto& book : recents)
        if (book.path == APP_STATE.openEpubPath) {
          append(book);
          break;
        }
    if (recentBooks.empty()) append(RecentBook{"", APP_STATE.openEpubPath, "", "", ""});
  }
  if (recentBooks.empty() && !recents.empty()) append(recents.front());
  // Stats are already stored most-recent-first, including completed books.
  for (const auto& stats : READING_STATS.getBooks()) {
    append(RecentBook{stats.bookId, stats.path, stats.title, stats.author, stats.coverBmpPath});
    if (recentBooks.size() == HOME_BOOK_LIMIT) break;
  }
  for (const auto& book : recents) {
    append(book);
    if (recentBooks.size() == HOME_BOOK_LIMIT) break;
  }

  HomeLayout layout(renderer);
  for (size_t i = 0; i < recentBooks.size(); ++i) {
    auto& book = recentBooks[i];
    if (book.title.empty()) {
      const size_t slash = book.path.find_last_of('/');
      book.title = book.path.substr(slash == std::string::npos ? 0 : slash + 1);
    }
    const int font = i == 0 && !layout.landscape ? titleFontId : (layout.landscape ? smallFontId : bodyFontId);
    const int width = i == 0 ? layout.currentWidth - 28 : layout.recentWidth - 128;
    bookTitles[i] = renderer.truncatedText(font, book.title.c_str(), width, EpdFontFamily::BOLD);
    bookAuthors[i] = renderer.truncatedText(smallFontId, book.author.c_str(), width);
    const auto* stats = bookStats(book);
    progress[i] = stats ? std::min<uint8_t>(stats->lastProgressPercent, 100) : 0;
    completed[i] = stats && stats->completed;
  }
  const auto* current = recentBooks.empty() ? nullptr : bookStats(recentBooks.front());
  totalTime = ReadingStatsAnalytics::formatDurationHm(current ? current->totalReadingMs : 0);
  lastSession = ReadingStatsAnalytics::formatDurationHm(current ? current->lastSessionMs : 0);
  char value[48];
  snprintf(value, sizeof(value), "%llu/%llu",
           static_cast<unsigned long long>(READING_STATS.getTodayReadingMs() / 60000),
           static_cast<unsigned long long>(getDailyReadingGoalMs() / 60000));
  dailyGoal = value;
  streak = std::to_string(READING_STATS.getCurrentStreakDays());
  finished = std::to_string(READING_STATS.getBooksFinishedCount());
  dashboardShortcuts.clear();
  for (const auto& definition : getShortcutDefinitions()) {
    if (definition.id == ShortcutId::Library || definition.id == ShortcutId::ReadingStats) continue;
    if (definition.id == ShortcutId::OpdsBrowser && !hasOpdsServers) continue;
    if (SETTINGS.*definition.locationPtr == CrossPointSettings::SHORTCUT_HOME && getShortcutVisibility(definition))
      dashboardShortcuts.push_back(&definition);
  }
  std::stable_sort(dashboardShortcuts.begin(), dashboardShortcuts.end(),
                   [](const auto* a, const auto* b) { return getShortcutOrder(*a) < getShortcutOrder(*b); });
  shortcutTitles.clear();
  shortcutTitles.reserve(dashboardShortcuts.size());
  for (const auto* definition : dashboardShortcuts)
    shortcutTitles.push_back(renderer.truncatedText(smallFontId, ShortcutUiMetadata::getName(*definition).c_str(),
                                                    layout.width - 64, EpdFontFamily::BOLD));
  selectorIndex = std::clamp(selectorIndex, 0, getMenuItemCount() - 1);
}

int BrumaHomeActivity::getMenuItemCount() const {
  return static_cast<int>(recentBooks.size()) + HOME_ACTION_COUNT + static_cast<int>(dashboardShortcuts.size());
}
void BrumaHomeActivity::onEnter() {
  Activity::onEnter();
  smallFontId = UI_10_FONT_ID;
  bodyFontId = titleFontId = UI_12_FONT_ID;
#ifndef OMIT_FONTS
  // One sans family for the Latin demo/UI; other scripts retain the firmware
  // UI stack and its fallback coverage. Slim builds also use that stack.
  if (I18N.getLanguage() == Language::ES || I18N.getLanguage() == Language::EN) {
    smallFontId = NOTOSANS_10_FONT_ID;
    bodyFontId = NOTOSANS_12_FONT_ID;
    titleFontId = NOTOSANS_18_FONT_ID;
  }
#endif
  hasOpdsServers = OPDS_STORE.hasServers();
  firstRenderDone = false;
  selectorIndex = 0;
  reloadDashboard();
  selectorIndex = indexForMenuItem(initialMenuItem);
  requestUpdate();
}
void BrumaHomeActivity::onExit() {
  Activity::onExit();
  recentBooks.clear();
  dashboardShortcuts.clear();
  shortcutTitles.clear();
}
int BrumaHomeActivity::indexForMenuItem(HomeMenuItem item) const {
  const int firstExtra = static_cast<int>(recentBooks.size()) + HOME_ACTION_COUNT;
  for (size_t i = 0; i < dashboardShortcuts.size(); ++i) {
    const auto id = dashboardShortcuts[i]->id;
    if ((item == HomeMenuItem::FILE_BROWSER && id == ShortcutId::BrowseFiles) ||
        (item == HomeMenuItem::RECENTS && id == ShortcutId::RecentBooks) ||
        (item == HomeMenuItem::OPDS_BROWSER && id == ShortcutId::OpdsBrowser) ||
        (item == HomeMenuItem::FILE_TRANSFER && id == ShortcutId::FileTransfer) ||
        (item == HomeMenuItem::SETTINGS_MENU && id == ShortcutId::Settings))
      return firstExtra + i;
  }
  return 0;
}
void BrumaHomeActivity::loop() {
  buttonNavigator.onNext([this] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, getMenuItemCount());
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, getMenuItemCount());
    requestUpdate();
  });
  if (handleTouch()) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && !recentBooks.empty()) {
    onSelectBook(recentBooks.front().path);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) activateSelection();
}
bool BrumaHomeActivity::handleTouch() {
  if (!mappedInput.hasTouch()) return false;
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = swipe == MappedInputManager::SwipeDir::Up
                        ? ButtonNavigator::nextIndex(selectorIndex, getMenuItemCount())
                        : ButtonNavigator::previousIndex(selectorIndex, getMenuItemCount());
    requestUpdate();
    return true;
  }
  HomeLayout l(renderer);
  const int books = static_cast<int>(recentBooks.size());
  const int extraStart = books + HOME_ACTION_COUNT;
  const auto hit = [this](int index, Rect rect) {
    const auto touch = mappedInput.rowTouch(rect.x, rect.y, rect.width, rect.height);
    if (touch == MappedInputManager::RowTouch::Down) {
      selectorIndex = index;
      requestUpdate();
      return true;
    }
    if (touch == MappedInputManager::RowTouch::Tap) {
      selectorIndex = index;
      activateSelection();
      return true;
    }
    return false;
  };
  if (selectorIndex >= extraStart) {
    const int start = (selectorIndex - extraStart) / 5 * 5;
    for (int i = start; i < std::min(start + 5, static_cast<int>(dashboardShortcuts.size())); ++i)
      if (hit(extraStart + i, Rect(l.x, l.top + 46 + (i - start) * 64, l.width, 60))) return true;
  } else {
    if (books > 0 && hit(0, Rect(l.x, l.top + 28, l.currentWidth, l.landscape ? 142 : l.recentTop - l.top - 46)))
      return true;
    for (int i = 1; i < books; ++i)
      if (hit(i, Rect(l.recentX, l.recentTop + (l.landscape ? 32 : 40) + (i - 1) * l.rowHeight, l.recentWidth,
                      l.rowHeight - 8)))
        return true;
    if (hit(books, Rect(l.x, l.statsTop, l.width, l.navTop - l.statsTop - 8))) return true;
    if (hit(books + 1, Rect(l.x, l.navTop, l.width / 2 - 4, 42))) return true;
    if (hit(books + 2, Rect(l.x + l.width / 2 + 4, l.navTop, l.width / 2 - 4, 42))) return true;
    if (hit(books + 3, Rect(l.x, l.clockTop, l.width, 36))) return true;
  }
  return false;
}
void BrumaHomeActivity::activateSelection() {
  const int books = static_cast<int>(recentBooks.size());
  if (selectorIndex < books) {
    onSelectBook(recentBooks[selectorIndex].path);
    return;
  }
  switch (selectorIndex - books) {
    case 0:
      onReadingStatsOpen();
      return;
    case 1:
      activityManager.goToLibrary();
      return;
    case 2:
      onAppsOpen();
      return;
    case 3:
      startActivityForResult(std::make_unique<ClockSyncActivity>(renderer, mappedInput), [this](const ActivityResult&) {
        reloadDashboard();
        requestUpdate();
      });
      return;
  }
  const int extraIndex = selectorIndex - books - HOME_ACTION_COUNT;
  if (extraIndex < 0 || extraIndex >= static_cast<int>(dashboardShortcuts.size())) return;
  switch (dashboardShortcuts[extraIndex]->id) {
    case ShortcutId::Stats:
      onReadingStatsOpen();
      break;
    case ShortcutId::ScreenClean:
      startActivityForResult(std::make_unique<ScreenCleanActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case ShortcutId::Library:
      activityManager.goToLibrary();
      break;
    case ShortcutId::BrowseFiles:
      onFileBrowserOpen();
      break;
    case ShortcutId::ReadingStats:
      onReadingStatsOpen();
      break;
    case ShortcutId::SyncDay:
      onSyncDayOpen();
      break;
    case ShortcutId::Settings:
      activityManager.goToSettings();
      break;
    case ShortcutId::ReadingHeatmap:
      startActivityForResult(std::make_unique<ReadingHeatmapActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               reloadDashboard();
                               requestUpdate();
                             });
      break;
    case ShortcutId::ReadingProfile:
      startActivityForResult(std::make_unique<ReadingProfileActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               reloadDashboard();
                               requestUpdate();
                             });
      break;
    case ShortcutId::Achievements:
      startActivityForResult(std::make_unique<AchievementsActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               reloadDashboard();
                               requestUpdate();
                             });
      break;
    case ShortcutId::IfFound:
      startActivityForResult(std::make_unique<IfFoundActivity>(renderer, mappedInput), [this](const ActivityResult&) {
        reloadDashboard();
        requestUpdate();
      });
      break;
    case ShortcutId::RecentBooks:
      activityManager.goToRecentBooks();
      break;
    case ShortcutId::Bookmarks:
      startActivityForResult(std::make_unique<BookmarksAppActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               reloadDashboard();
                               requestUpdate();
                             });
      break;
    case ShortcutId::Favorites:
      startActivityForResult(std::make_unique<FavoritesAppActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               reloadDashboard();
                               requestUpdate();
                             });
      break;
    case ShortcutId::Flashcards:
      startActivityForResult(std::make_unique<FlashcardsAppActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               reloadDashboard();
                               requestUpdate();
                             });
      break;
    case ShortcutId::Dictionary:
      startActivityForResult(std::make_unique<DictionaryActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               reloadDashboard();
                               requestUpdate();
                             });
      break;
    case ShortcutId::FileTransfer:
      activityManager.goToFileTransfer();
      break;
    case ShortcutId::Sleep:
      startActivityForResult(std::make_unique<SleepAppActivity>(renderer, mappedInput), [this](const ActivityResult&) {
        reloadDashboard();
        requestUpdate();
      });
      break;
    case ShortcutId::OpdsBrowser:
      onOpdsBrowserOpen();
      break;
  }
}

void BrumaHomeActivity::render(RenderLock&&) {
  HomeLayout l(renderer);
  const auto& m = UITheme::getInstance().getMetrics();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect(0, m.topPadding, renderer.getScreenWidth(), 40), nullptr, nullptr);
  HeaderDateUtils::drawTopLine(renderer, HeaderDateUtils::getDisplayDateText());
  const int books = static_cast<int>(recentBooks.size());
  const int extraStart = books + HOME_ACTION_COUNT;
  const auto text = [this](int font, int x, int y, const char* value, bool bold = false, bool black = true) {
    renderer.drawText(font, x, y, value, black, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  };
  freeink::ui::GfxRendererTarget iconTarget(renderer);
  const auto button = [&](Rect rect, const char* label, int index, UIIcon type) {
    const bool selected = selectorIndex == index;
    if (selected) renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
    renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
    const int tw = renderer.getTextWidth(smallFontId, label, EpdFontFamily::BOLD);
    const auto* artwork = homeIcon(type);
    const int iconBand = artwork ? 34 : 0;
    const int left = rect.x + (rect.width - tw - iconBand) / 2;
    const int centerY = rect.y + rect.height / 2;
    if (artwork) {
      iconTarget.bitmap(freeink::ui::Rect{static_cast<int16_t>(left),
                                          static_cast<int16_t>(centerY - artwork->opticalCenterY), 24, 24},
                        freeink::ui::bitmapFromIcon(*artwork), freeink::ui::BitmapMode::Contain,
                        freeink::ui::Paint::solid(freeink::ui::Color::Black));
    }
    text(smallFontId, left + iconBand, centerY - textInkCenter(renderer, smallFontId, label), label, true);
  };
  if (selectorIndex >= extraStart) {
    text(bodyFontId, l.x, l.top, tr(STR_SHORTCUTS_SECTION), true);
    const int start = (selectorIndex - extraStart) / 5 * 5;
    for (int i = start; i < std::min(start + 5, static_cast<int>(dashboardShortcuts.size())); ++i)
      button(Rect(l.x, l.top + 46 + (i - start) * 64, l.width, 54), shortcutTitles[i].c_str(), extraStart + i,
             dashboardShortcuts[i]->icon);
  } else {
    text(smallFontId, l.x, l.top, tr(STR_HOME_CURRENT_BOOK), true);
    if (books) {
      if (selectorIndex == 0) {
        const int cardHeight = l.landscape ? 144 : l.recentTop - l.top - 46;
        renderer.fillRectDither(l.x, l.top + 33, l.currentWidth, cardHeight, Color::LightGray);
        renderer.drawRect(l.x, l.top + 33, l.currentWidth, cardHeight);
      }
      text(l.landscape ? bodyFontId : titleFontId, l.x + 14, l.top + 36, bookTitles[0].c_str(), true);
      text(smallFontId, l.x + 14, l.top + 76, bookAuthors[0].c_str());
      char pct[8];
      snprintf(pct, sizeof(pct), "%u%%", progress[0]);
      const int barY = l.top + (l.landscape ? 104 : 110);
      drawProgress(renderer, l.x + 14, barY, l.currentWidth - 106, progress[0]);
      text(bodyFontId, l.x + l.currentWidth - 72, barY - 10, pct, true);
      const int metricY = barY + 24;
      if (l.landscape) {
        // One compact line keeps the X3/landscape home readable.
        text(smallFontId, l.x + 14, metricY, totalTime.c_str(), true);
        text(smallFontId, l.x + 110, metricY, lastSession.c_str());
      } else {
        text(smallFontId, l.x + 14, metricY, tr(STR_TOTAL_TIME));
        text(smallFontId, l.x + l.currentWidth / 2 + 8, metricY, tr(STR_LAST_SESSION));
        text(bodyFontId, l.x + 14, metricY + 22, totalTime.c_str(), true);
        text(bodyFontId, l.x + l.currentWidth / 2 + 8, metricY + 22, lastSession.c_str(), true);
      }
    } else
      text(bodyFontId, l.x, l.top + 65, tr(STR_NO_RECENT_BOOKS));
    if (!l.landscape) renderer.drawLine(l.x, l.recentTop - 4, l.x + l.width, l.recentTop - 4);
    text(smallFontId, l.recentX, l.recentTop, tr(STR_HOME_RECENT_READS), true);
    for (int i = 1; i < books; ++i) {
      const int y = l.recentTop + (l.landscape ? 32 : 40) + (i - 1) * l.rowHeight;
      const bool selected = selectorIndex == i;
      if (selected) {
        renderer.fillRectDither(l.recentX, y - 4, l.recentWidth, l.rowHeight - 4, Color::LightGray);
        renderer.drawRect(l.recentX, y - 4, l.recentWidth, l.rowHeight - 4);
      }
      text(l.landscape ? smallFontId : bodyFontId, l.recentX + 14, y, bookTitles[i].c_str(), true);
      if (!l.landscape) text(smallFontId, l.recentX + 14, y + 27, bookAuthors[i].c_str());
      char pct[8];
      snprintf(pct, sizeof(pct), "%u%%", progress[i]);
      text(l.landscape ? smallFontId : bodyFontId, l.recentX + l.recentWidth - 82, y, pct, true);
      const char* state = completed[i] ? tr(STR_HOME_COMPLETED) : tr(STR_IN_PROGRESS);
      const int sw = renderer.getTextWidth(SMALL_FONT_ID, state);
      text(SMALL_FONT_ID, l.recentX + l.recentWidth - 14 - sw, y + (l.landscape ? 22 : 31), state);
      drawProgress(renderer, l.recentX + 14, y + l.rowHeight - (l.landscape ? 16 : 30), l.recentWidth - 28, progress[i]);
    }
    if (books <= 1) text(smallFontId, l.recentX, l.recentTop + 42, tr(STR_NO_RECENT_BOOKS));
    renderer.drawLine(l.x, l.statsTop - 12, l.x + l.width, l.statsTop - 12);
    text(smallFontId, l.x, l.statsTop, tr(STR_HOME_MY_STATS), true);
    const int col = l.width / 3;
    const char* labels[] = {tr(STR_HOME_TODAY), tr(STR_HOME_STREAK), tr(STR_HOME_FINISHED)};
    const char* values[] = {dailyGoal.c_str(), streak.c_str(), finished.c_str()};
    const char* units[] = {tr(STR_READING_TIME_MINUTES), tr(STR_HOME_DAYS), tr(STR_HOME_BOOKS)};
    for (int i = 0; i < 3; ++i) {
      const int x = l.x + i * col + 8;
      if (i) renderer.drawLine(x - 12, l.metricsTop, x - 12, l.detailsTop - 12);
      text(smallFontId, x, l.metricsTop, labels[i]);
      text(bodyFontId, x, l.metricsTop + (l.landscape ? 19 : 26), values[i], true);
      text(SMALL_FONT_ID, x, l.metricsTop + (l.landscape ? 44 : 57), units[i]);
    }
    button(Rect(l.x, l.detailsTop, l.width, 40), tr(STR_HOME_FULL_STATS), books, UIIcon::Book);
    renderer.drawLine(l.x, l.navTop - 6, l.x + l.width, l.navTop - 6);
    button(Rect(l.x, l.navTop, l.width / 2 - 4, 40), tr(STR_LIBRARY), books + 1, UIIcon::Library);
    button(Rect(l.x + l.width / 2 + 4, l.navTop, l.width / 2 - 4, 40), tr(STR_APPS), books + 2, UIIcon::Apps);
    button(Rect(l.x, l.clockTop, l.width, 34), tr(STR_HOME_REFRESH_TIME), books + 3, UIIcon::Recent);
  }
  const auto labels =
      mappedInput.mapLabels(books ? tr(STR_RESUME) : "", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  firstRenderDone = true;
}
void BrumaHomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }
void BrumaHomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }
void BrumaHomeActivity::onAppsOpen() { activityManager.goToApps(); }
void BrumaHomeActivity::onReadingStatsOpen() {
  startActivityForResult(std::make_unique<ReadingStatsActivity>(renderer, mappedInput), [this](const ActivityResult&) {
    reloadDashboard();
    requestUpdate();
  });
}
void BrumaHomeActivity::onSyncDayOpen() {
  startActivityForResult(std::make_unique<SyncDayActivity>(renderer, mappedInput), [this](const ActivityResult&) {
    reloadDashboard();
    requestUpdate();
  });
}
void BrumaHomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
