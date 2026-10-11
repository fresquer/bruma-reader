#include "FontInstallActivity.h"

#include <FontCacheManager.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "SdCardFontGlobals.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void FontInstallActivity::onEnter() {
  Activity::onEnter();
  if (!sourcePath.empty()) {
    install();
    return;
  }
  startActivityForResult(
      std::make_unique<FileBrowserActivity>(renderer, mappedInput, "/", FileBrowserActivity::Mode::PickFont),
      [this](const ActivityResult& result) {
        const auto* file = std::get_if<FilePathResult>(&result.data);
        if (result.isCancelled || !file || file->path.empty()) {
          finish();
          return;
        }
        sourcePath = file->path;
        install();
      });
}

void FontInstallActivity::install(const bool replaceExisting) {
  {
    RenderLock lock(*this);
    const size_t slash = sourcePath.find_last_of('/');
    displayName =
        renderer.truncatedText(UI_10_FONT_ID, sourcePath.substr(slash + 1).c_str(), renderer.getScreenWidth() - 48);
    messageLines.clear();
    installing = true;
  }
  requestUpdateAndWait();
  FontInstaller::Error error;
  {
    // The render task can use the active SD font for UI fallback. Protect its
    // cache lifetime and any reload while the prepared file is being replaced.
    RenderLock lock(*this);
    if (auto* cache = renderer.getFontCacheManager()) cache->releaseSdFontCaches();
    FontInstaller installer(sdFontSystem.registry());
    error = installer.installFromSd(sourcePath, replaceExisting);
    if (error == FontInstaller::Error::OK) {
      sdFontSystem.markRegistryDirty();
      ensureSdFontLoaded();
      SETTINGS.saveToFile();
    }
  }
  if (error == FontInstaller::Error::FILE_EXISTS) {
    {
      RenderLock lock(*this);
      installing = false;
    }
    startActivityForResult(
        std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_REPLACE_FONT), displayName),
        [this](const ActivityResult& result) {
          if (result.isCancelled)
            finish();
          else
            install(true);
        });
    return;
  }
  {
    RenderLock lock(*this);
    installing = false;
    success = error == FontInstaller::Error::OK;
    const char* message = success ? tr(STR_FONT_SD_INSTALLED_HINT) : tr(STR_FONT_INSTALL_FAILED);
    if (error == FontInstaller::Error::INVALID_FILE || error == FontInstaller::Error::INVALID_FAMILY_NAME)
      message = tr(STR_FONT_FILE_INVALID);
    else if (error == FontInstaller::Error::SD_WRITE_ERROR)
      message = tr(STR_FONT_SD_WRITE_ERROR);
    else if (error == FontInstaller::Error::MAX_FAMILIES_REACHED)
      message = tr(STR_TOO_MANY_FONTS);
    messageLines =
        renderer.wrappedText(UI_10_FONT_ID, message, renderer.getScreenWidth() - 48, 4, EpdFontFamily::REGULAR);
  }
  requestUpdate();
}

void FontInstallActivity::loop() {
  if (!installing && (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
                      mappedInput.wasReleased(MappedInputManager::Button::Confirm)))
    finish();
}

void FontInstallActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& m = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect(0, m.topPadding, renderer.getScreenWidth(), m.headerHeight), tr(STR_INSTALL_FONT_FILE));
  int y = renderer.getScreenHeight() / 2 - 70;
  renderer.drawCenteredText(
      UI_12_FONT_ID, y,
      installing ? tr(STR_IMPORTING) : (success ? tr(STR_FONT_INSTALLED) : tr(STR_FONT_INSTALL_FAILED)), true,
      EpdFontFamily::BOLD);
  y += 50;
  renderer.drawCenteredText(UI_10_FONT_ID, y, displayName.c_str());
  y += 45;
  for (const auto& line : messageLines) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str());
    y += renderer.getLineHeight(UI_10_FONT_ID) + 6;
  }
  const auto labels = mappedInput.mapLabels(installing ? "" : tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
