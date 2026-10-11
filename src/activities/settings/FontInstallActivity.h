#pragma once

#include <string>
#include <vector>

#include "FontInstaller.h"
#include "activities/Activity.h"

class FontInstallActivity final : public Activity {
 public:
  explicit FontInstallActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path = "")
      : Activity("FontInstall", renderer, mappedInput), sourcePath(std::move(path)) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return installing; }

 private:
  std::string sourcePath;
  std::string displayName;
  std::vector<std::string> messageLines;
  bool installing = false;
  bool success = false;
  void install(bool replaceExisting = false);
};
