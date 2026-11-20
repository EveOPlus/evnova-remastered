#pragma once

#include <SDL3/SDL.h>
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class SdlPlatform;

namespace game {
class NovaFontCache;

struct NarrationVoice {
  std::string_view id;
  std::string_view name;
  std::string_view language;
};

inline constexpr std::array<NarrationVoice, 4> kNarrationVoices{{
    {"en_US-lessac-medium", "Lessac", "American English"},
    {"en_US-ryan-medium", "Ryan", "American English"},
    {"en_GB-alba-medium", "Alba", "British English"},
    {"en_GB-alan-medium", "Alan", "British English"},
}};

// Remake-only local narration. Lifetime belongs to the platform, not a global.
class MissionNarrator {
public:
  MissionNarrator();
  ~MissionNarrator();
  std::size_t Open();
  void SetText(std::size_t token, std::string text);
  void Close(std::size_t token);
  bool HandleEvent(SdlPlatform &, const SDL_Event &);
  void Draw(SdlPlatform &);
  void AddControls(SdlPlatform &,
                   std::vector<std::pair<std::string, SDL_FRect>> &) const;

  [[nodiscard]] bool active() const { return !dialogs_.empty(); }

  [[nodiscard]] bool enabled() const { return enabled_; }

  [[nodiscard]] std::string_view voice() const { return voice_; }

  [[nodiscard]] bool VoiceAvailable(std::string_view id) const;
  bool SelectVoice(std::string_view id);
  void SetEnabled(bool enabled);
  void PreviewVoice();
  void Stop();
  bool InstallVoices();
  void PollInstallation();

  [[nodiscard]] bool installing() const { return installer_ != nullptr; }

  [[nodiscard]] const std::string &install_status() const {
    return install_status_;
  }

  [[nodiscard]] std::string Snapshot() const;

private:
  struct Dialog {
    std::size_t token;
    std::string text;
  };

  void Send(bool stop = false);
  void SendText(std::string_view text);
  bool StartWorker();
  std::vector<Dialog> dialogs_;
  std::size_t token_ = 0;
  std::size_t request_ = 0;
  bool enabled_ = true;
  std::string voice_{kNarrationVoices.front().id};
  SDL_Process *process_ = nullptr;
  std::filesystem::path preferences_;
  std::filesystem::path voice_preferences_;
  std::filesystem::path state_;
  std::unique_ptr<NovaFontCache> fonts_;
  std::unique_ptr<SDL_Process, decltype(&SDL_DestroyProcess)> installer_{
      nullptr, SDL_DestroyProcess};
  std::string install_status_;
};

void NovaNarration_RunVoiceSettings(
    SdlPlatform &,
    NovaFontCache &,
    const std::function<void()> &render_background);

class MissionNarrationScope {
public:
  explicit MissionNarrationScope(SdlPlatform &, bool active = true);
  ~MissionNarrationScope();
  MissionNarrationScope(const MissionNarrationScope &) = delete;
  MissionNarrationScope &operator=(const MissionNarrationScope &) = delete;
  void SetText(std::string text);

private:
  MissionNarrator *narrator_ = nullptr;
  std::size_t token_ = 0;
};
} // namespace game
