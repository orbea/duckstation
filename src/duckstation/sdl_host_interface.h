#pragma once
#include "YBaseLib/String.h"
#include "YBaseLib/Timer.h"
#include "common/gl/program.h"
#include "common/gl/texture.h"
#include "core/game_list.h"
#include "core/host_display.h"
#include "core/host_interface.h"
#include <SDL.h>
#include <array>
#include <deque>
#include <map>
#include <memory>
#include <mutex>

class System;
class DigitalController;
class MemoryCard;
class AudioStream;

class SDLHostInterface final : public HostInterface
{
public:
  SDLHostInterface();
  ~SDLHostInterface();

  static std::unique_ptr<SDLHostInterface> Create(const char* filename = nullptr, const char* exp1_filename = nullptr,
                                                  const char* save_state_filename = nullptr);

  static TinyString GetSaveStateFilename(u32 index);

  void ReportError(const char* message) override;
  void ReportMessage(const char* message) override;

  void Run();

protected:
  void ConnectControllers() override;

private:
  static constexpr u32 NUM_QUICK_SAVE_STATES = 10;
  static constexpr char RESUME_SAVESTATE_FILENAME[] = "savestate_resume.bin";

  bool HasSystem() const { return static_cast<bool>(m_system); }

#ifdef WIN32
  bool UseOpenGLRenderer() const { return m_settings.gpu_renderer == GPURenderer::HardwareOpenGL; }
#else
  bool UseOpenGLRenderer() const { return true; }
#endif

  bool CreateSDLWindow();
  void DestroySDLWindow();
  bool CreateDisplay();
  void DestroyDisplay();
  void CreateImGuiContext();
  bool CreateAudioStream();
  void LoadGameList();

  void OpenGameControllers();
  void CloseGameControllers();

  void SaveSettings();

  void QueueSwitchGPURenderer();
  void SwitchGPURenderer();
  void UpdateFullscreen();

  // We only pass mouse input through if it's grabbed
  void DrawImGui();
  void DoPowerOff();
  void DoResume();
  void DoStartDisc();
  void DoStartBIOS();
  void DoChangeDisc();
  void DoLoadState(u32 index);
  void DoSaveState(u32 index);
  void DoTogglePause();
  void DoFrameStep();
  void DoToggleSoftwareRendering();
  void DoToggleFullscreen();
  void DoModifyInternalResolution(s32 increment);

  void HandleSDLEvent(const SDL_Event* event);
  void HandleSDLKeyEvent(const SDL_Event* event);

  void DrawMainMenuBar();
  void DrawQuickSettingsMenu();
  void DrawDebugMenu();
  void DrawPoweredOffWindow();
  void DrawGameListWindow();
  void DrawSettingsWindow();
  void DrawAboutWindow();
  void DrawDebugWindows();
  bool DrawFileChooser(const char* label, std::string* path, const char* filter = nullptr);

  SDL_Window* m_window = nullptr;
  std::unique_ptr<HostDisplayTexture> m_app_icon_texture;

  std::string m_settings_filename;

  std::map<int, SDL_GameController*> m_sdl_controllers;

  std::shared_ptr<DigitalController> m_controller;

  u32 m_switch_gpu_renderer_event_id = 0;

  bool m_quit_request = false;
  bool m_frame_step_request = false;
  bool m_focus_main_menu_bar = false;
  bool m_settings_window_open = false;
  bool m_about_window_open = false;

  GameList m_game_list;
};
