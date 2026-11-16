#pragma once

#include <memory>

namespace rex::ui {
class ImGuiDrawer;
class Window;
}  // namespace rex::ui

// Hitz-style ESC quit + F1 video settings overlay (ImGui), wired for ReXGlue.
class NbaHostOverlay {
 public:
  virtual ~NbaHostOverlay() = default;
  virtual void ApplyIniFile() = 0;
};

std::unique_ptr<NbaHostOverlay> CreateNbaHostOverlay(rex::ui::ImGuiDrawer* drawer,
                                                     rex::ui::Window* window);

// Apply %LOCALAPPDATA%\NBAUnrivaled\settings.ini [display] into live cvars.
void NbaApplyHostSettingsIni(NbaHostOverlay* overlay = nullptr);
