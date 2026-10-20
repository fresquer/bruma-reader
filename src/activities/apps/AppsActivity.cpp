#include "AppsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "AchievementsActivity.h"
#include "BookmarksAppActivity.h"
#include "CrossPointSettings.h"
#include "DictionaryActivity.h"
#include "FavoritesAppActivity.h"
#include "IfFoundActivity.h"
#include "ReadingHeatmapActivity.h"
#include "ReadingProfileActivity.h"
#include "ReadingStatsActivity.h"
#include "ScreenCleanActivity.h"
#include "SleepAppActivity.h"
#include "SyncDayActivity.h"
#include "activities/settings/ClockSyncActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/HeaderDateUtils.h"
#include "util/ShortcutUiMetadata.h"

namespace fui = freeink::ui;
fui::ListNav AppsActivity::savedNav;
ShortcutId AppsActivity::savedShortcut = ShortcutId::Library;
bool AppsActivity::selectionSaved = false;

namespace {
std::string buildAppsHeaderSubtitle(const int selectedIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0) {
    return "";
  }

  const int safeItemsPerPage = std::max(1, itemsPerPage);
  const int currentPage = std::clamp(selectedIndex, 0, totalItems - 1) / safeItemsPerPage + 1;
  const int totalPages = (totalItems + safeItemsPerPage - 1) / safeItemsPerPage;
  return std::to_string(currentPage) + "/" + std::to_string(totalPages) + " | " + std::to_string(totalItems);
}
}  // namespace

void AppsActivity::loadShortcuts() {
  appShortcuts = getConfiguredShortcuts(CrossPointSettings::SHORTCUT_APPS);
  rebuildRows();
}

// Derives the row cache from appShortcuts. Called whenever the shortcut list
// is (re)loaded, never from buildScreen(), which reuses the rows on repaint.
void AppsActivity::rebuildRows() {
  shortcutNames.clear();
  shortcutSubtitles.clear();
  rowItems.clear();
  shortcutNames.reserve(appShortcuts.size());
  shortcutSubtitles.reserve(appShortcuts.size());
  rowItems.reserve(appShortcuts.size());

  for (const ShortcutDefinition* definition : appShortcuts) {
    if (definition == nullptr) {
      shortcutNames.emplace_back();
      shortcutSubtitles.emplace_back();
    } else {
      shortcutNames.push_back(ShortcutUiMetadata::getName(*definition));
      shortcutSubtitles.push_back(ShortcutUiMetadata::getSubtitle(*definition));
    }
  }

  for (size_t i = 0; i < appShortcuts.size(); ++i) {
    fui::ListItem item;
    item.label = shortcutNames[i].c_str();
    if (!shortcutSubtitles[i].empty()) item.subtitle = shortcutSubtitles[i].c_str();
    if (appShortcuts[i] != nullptr) item.icon = listIconFor(appShortcuts[i]->icon, 32);
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void AppsActivity::restoreShortcut() {
  if (!selectionSaved) return;
  const auto selected = std::find_if(appShortcuts.begin(), appShortcuts.end(),
                                     [](const auto* definition) { return definition->id == savedShortcut; });
  if (selected != appShortcuts.end()) nav.selected = static_cast<int>(selected - appShortcuts.begin());
  nav.follow(listCount());
}

bool AppsActivity::handleCustomInput() {
  if (!waitForBackRelease) return false;
  // Some apps exit on Back press. Consume its release here so it cannot
  // immediately leave the restored Apps screen as well.
  if (!mappedInput.isPressed(MappedInputManager::Button::Back)) waitForBackRelease = false;
  return true;
}

void AppsActivity::onEnter() {
  UiListActivity::onEnter();
  waitForBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
  loadShortcuts();
  nav = savedNav;
  restoreShortcut();
}

void AppsActivity::onExit() {
  Activity::onExit();
  // rowItems alias the name/subtitle strings; drop them together.
  rowItems.clear();
  shortcutNames.clear();
  shortcutSubtitles.clear();
  appShortcuts.clear();
}

void AppsActivity::drawChrome() {
  const std::string headerSubtitle = buildAppsHeaderSubtitle(nav.selected, listCount(), nav.pageRowsFor(listCount()));
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_APPS),
                                      headerSubtitle.empty() ? nullptr : headerSubtitle.c_str());
}

void AppsActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void AppsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the header band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (appShortcuts.empty()) {
    screen.centeredText(tr(STR_NO_ENTRIES), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  fui::TextStyle label = screen.theme().smallText;
  label.bold = true;  // title/description hierarchy; also the caller-owned marker
  props.labelText = label;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void AppsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  // Opening an app leaves this screen; a lingering flash would gray an
  // unrelated row when the hub reappears.
  app.clearTapFlash();
  nav.selected = index;
  selectionSaved = true;
  savedNav = nav;
  savedShortcut = appShortcuts[index]->id;
  openApp(index);
}

void AppsActivity::openApp(const int index) {
  std::unique_ptr<Activity> activity;
  switch (appShortcuts[index]->id) {
    case ShortcutId::Library:
      activityManager.goToLibrary();
      return;
    case ShortcutId::BrowseFiles:
      activityManager.goToFileBrowser();
      return;
    case ShortcutId::ReadingStats:
      activity = std::make_unique<ReadingStatsActivity>(renderer, mappedInput);
      break;
    case ShortcutId::SyncDay:
      if (SETTINGS.isHardwareRtcAutoDayClockActive()) {
        activity = std::make_unique<ClockSyncActivity>(renderer, mappedInput);
      } else {
        activity = std::make_unique<SyncDayActivity>(renderer, mappedInput);
      }
      break;
    case ShortcutId::Settings:
      activityManager.goToSettings();
      return;
    case ShortcutId::ReadingHeatmap:
      activity = std::make_unique<ReadingHeatmapActivity>(renderer, mappedInput);
      break;
    case ShortcutId::ReadingProfile:
      activity = std::make_unique<ReadingProfileActivity>(renderer, mappedInput);
      break;
    case ShortcutId::Achievements:
      activity = std::make_unique<AchievementsActivity>(renderer, mappedInput);
      break;
    case ShortcutId::IfFound:
      activity = std::make_unique<IfFoundActivity>(renderer, mappedInput);
      break;
    case ShortcutId::RecentBooks:
      activityManager.goToRecentBooks();
      return;
    case ShortcutId::Bookmarks:
      activity = std::make_unique<BookmarksAppActivity>(renderer, mappedInput);
      break;
    case ShortcutId::Favorites:
      activity = std::make_unique<FavoritesAppActivity>(renderer, mappedInput);
      break;
    case ShortcutId::Dictionary:
      activity = std::make_unique<DictionaryActivity>(renderer, mappedInput);
      break;
    case ShortcutId::FileTransfer:
      activityManager.goToFileTransfer();
      return;
    case ShortcutId::ScreenClean:
      activity = std::make_unique<ScreenCleanActivity>(renderer, mappedInput);
      break;
    case ShortcutId::Sleep:
      activity = std::make_unique<SleepAppActivity>(renderer, mappedInput);
      break;
    case ShortcutId::OpdsBrowser:
      activityManager.goToBrowser();
      return;
  }

  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    // The shortcut set can change underneath (Settings > Shortcuts); the
    // interaction table still indexes the old rows until the next render.
    closeRouting();
    waitForBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
    {
      RenderLock lock(*this);
      loadShortcuts();
      restoreShortcut();
    }
    requestUpdate();
  });
}
