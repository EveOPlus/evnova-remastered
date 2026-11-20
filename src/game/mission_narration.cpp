#include "mission_narration.hpp"

#include "../log.hpp"
#include "../nova_paths.hpp"
#include "../sdl_platform.hpp"
#include "nova_font.hpp"
#include "preferences_ui.hpp"
#include <algorithm>
#include <fmt/format.h>
#include <fstream>
#include <iterator>

namespace game {
namespace {
bool KnownVoice(std::string_view id) {
  return std::any_of(kNarrationVoices.begin(),
                     kNarrationVoices.end(),
                     [id](const auto &voice) { return voice.id == id; });
}

std::filesystem::path VoicePython(const std::filesystem::path &root) {
  const auto portable = root / ".cache/narrator/python.exe";
  return std::filesystem::exists(portable)
             ? portable
             : root / ".cache/narrator/Scripts/python.exe";
}

std::filesystem::path VoiceInstallRoot() {
  const auto support = NovaPaths::SharedSupportDirectory();
  return support ? *support / "NarrationRuntime" : std::filesystem::path{};
}

std::filesystem::path VoiceRoot(std::string_view id) {
  std::vector<std::filesystem::path> roots;
  if (const auto shared = VoiceInstallRoot(); !shared.empty())
    roots.push_back(shared);
  auto root = std::filesystem::path{SDL_GetBasePath()};
#if EVNOVA_ENABLE_PROBE
  // Reproduce a downloaded package without inheriting the developer's voices.
  if (SDL_getenv("EVN_PROBE") && SDL_getenv("EVN_PROBE_FRESH_VOICES"))
    root.clear();
#endif
  for (int depth = 0; depth < 6 && !root.empty();
       ++depth, root = root.parent_path())
    roots.push_back(root);
  for (const auto &candidate : roots) {
    if (std::filesystem::exists(VoicePython(candidate)) &&
        std::filesystem::exists(candidate / "tools/nova_narrator.py") &&
        std::filesystem::exists(candidate / ".cache/narrator-voices" /
                                (std::string(id) + ".onnx")) &&
        std::filesystem::exists(candidate / ".cache/narrator-voices" /
                                (std::string(id) + ".onnx.json")))
      return candidate;
  }
  return {};
}

std::vector<std::pair<std::string, SDL_FRect>> Controls(SdlPlatform &platform) {
  const auto size = platform.logical_playfield_size();
  const float scale = std::min(platform.ui_scale(), size.x / 560.0F);
  const float width = 170.0F * scale;
  const float gap = 8.0F * scale;
  const float y = size.y - 40.0F * scale;
  const float x = (size.x - width * 3 - gap * 2) * 0.5F;
  return {{"narration.replay", {x, y, width, 30.0F * scale}},
          {"narration.stop", {x + width + gap, y, width, 30.0F * scale}},
          {"narration.mute", {x + 2 * (width + gap), y, width, 30.0F * scale}}};
}
} // namespace

MissionNarrator::MissionNarrator() {
  if (const auto directory = NovaPaths::SupportSubdirectory("Narration")) {
    preferences_ = *directory / "enabled.txt";
    voice_preferences_ = *directory / "voice.txt";
    state_ = *directory / "state.json";
    std::ifstream in(preferences_);
    int enabled = 1;
    if (in >> enabled)
      enabled_ = enabled != 0;
    std::ifstream voice_in(voice_preferences_);
    std::string saved;
    if (voice_in >> saved && KnownVoice(saved) && VoiceAvailable(saved)) {
      voice_ = std::move(saved);
    }
  }
}

bool MissionNarrator::VoiceAvailable(std::string_view id) const {
  if (!KnownVoice(id)) {
    return false;
  }
  return !VoiceRoot(id).empty();
}

bool MissionNarrator::SelectVoice(std::string_view id) {
  if (!VoiceAvailable(id)) {
    return false;
  }
  if (voice_ == id) {
    return true;
  }
  Send(true);
  voice_ = id;
  if (!voice_preferences_.empty()) {
    std::ofstream out(voice_preferences_);
    if (!(out << voice_)) {
      NovaLog::Warn("narration voice preference could not be saved: {}",
                    voice_preferences_.string());
    }
  }
  Send();
  return true;
}

void MissionNarrator::SetEnabled(bool enabled) {
  enabled_ = enabled;
  if (!preferences_.empty()) {
    std::ofstream(preferences_) << (enabled_ ? 1 : 0);
  }
  Send();
}

void MissionNarrator::PreviewVoice() {
  SendText("Welcome aboard, Captain. Your ship is ready. A new mission awaits "
           "among the stars.");
}

void MissionNarrator::Stop() { Send(true); }

MissionNarrator::~MissionNarrator() {
  if (process_) {
    Send(true);
    // Closing the pipe signals EOF to the helper and stops audio even when
    // the application exits from inside a mission dialog.
    SDL_DestroyProcess(process_);
  }
}

bool MissionNarrator::InstallVoices() {
#ifdef _WIN32
  if (installer_)
    return false;
  const auto destination = VoiceInstallRoot();
  if (destination.empty())
    return false;
  auto root = std::filesystem::path{SDL_GetBasePath()};
  for (int depth = 0; depth < 6 && !root.empty();
       ++depth, root = root.parent_path()) {
    const auto script = root / "tools/setup-narration.ps1";
    if (!std::filesystem::is_regular_file(script))
      continue;
    std::error_code error;
    std::filesystem::create_directories(destination, error);
    if (error)
      break;
    Stop();
    const auto script_arg = script.string();
    const auto destination_arg = destination.string();
    const char *args[] = {"powershell.exe",
                          "-NoProfile",
                          "-NonInteractive",
                          "-WindowStyle",
                          "Hidden",
                          "-ExecutionPolicy",
                          "Bypass",
                          "-File",
                          script_arg.c_str(),
                          "-Destination",
                          destination_arg.c_str(),
                          nullptr};
    install_status_ = "Preparing voice installation...";
    std::ofstream(destination / "setup-status.txt") << install_status_;
    const auto properties = SDL_CreateProperties();
    SDL_SetPointerProperty(
        properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args);
    SDL_SetBooleanProperty(
        properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    installer_.reset(SDL_CreateProcessWithProperties(properties));
    SDL_DestroyProperties(properties);
    if (installer_)
      return true;
    NovaLog::Warn("voice installer could not start: {}", SDL_GetError());
    break;
  }
#endif
  install_status_ = "Could not start. Keep the tools folder beside the game.";
  return false;
}

void MissionNarrator::PollInstallation() {
  const auto root = VoiceInstallRoot();
  if (!root.empty()) {
    std::ifstream status(root / "setup-status.txt");
    std::string line;
    if (std::getline(status, line) && !line.empty())
      install_status_ = std::move(line);
  }
  if (installer_ && SDL_WaitProcess(installer_.get(), false, nullptr))
    installer_.reset();
}

bool MissionNarrator::StartWorker() {
  if (process_)
    return true;
  if (state_.empty())
    return false;
  const auto root = VoiceRoot(voice_);
  if (!root.empty()) {
    const auto python = VoicePython(root);
    const auto script = root / "tools/nova_narrator.py";
    const auto model = root / ".cache/narrator-voices" / (voice_ + ".onnx");
    if (!std::filesystem::exists(python) || !std::filesystem::exists(script) ||
        !std::filesystem::exists(model))
      return false;
    const auto python_arg = python.string(), script_arg = script.string();
    const auto model_arg = model.string(), state_arg = state_.string();
    const bool silent =
        SDL_GetCurrentAudioDriver() &&
        std::string_view(SDL_GetCurrentAudioDriver()) == "dummy";
    const char *args[] = {python_arg.c_str(),
                          script_arg.c_str(),
                          "--model",
                          model_arg.c_str(),
                          "--state",
                          state_arg.c_str(),
                          silent ? "--silent" : nullptr,
                          nullptr};
    const auto properties = SDL_CreateProperties();
    SDL_SetPointerProperty(
        properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args);
    SDL_SetBooleanProperty(
        properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_SetNumberProperty(properties,
                          SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                          SDL_PROCESS_STDIO_APP);
    process_ = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (!process_)
      NovaLog::Warn("mission narrator could not start: {}", SDL_GetError());
    return process_ != nullptr;
  }
  NovaLog::Warn("mission narrator runtime not installed");
  return false;
}

void MissionNarrator::Send(bool stop) {
  const bool speak =
      !stop && enabled_ && !dialogs_.empty() && !dialogs_.back().text.empty();
  SendText(speak ? std::string_view(dialogs_.back().text) : std::string_view{});
}

void MissionNarrator::SendText(std::string_view spoken_text) {
  if (!process_ && (spoken_text.empty() || !StartWorker()))
    return;
  std::string text;
  if (!spoken_text.empty()) {
    // Resource strings use MacRoman; narration consumes exactly the expanded
    // visible text, converted once at this output boundary.
    for (const unsigned char byte : NovaText_MacRomanToUtf8(spoken_text))
      text += fmt::format("{:02x}", byte);
  }
  const auto message = fmt::format("{}\t{}\t{}\n", ++request_, voice_, text);
  auto *input = SDL_GetProcessInput(process_);
  if (!input ||
      SDL_WriteIO(input, message.data(), message.size()) != message.size() ||
      !SDL_FlushIO(input))
    NovaLog::Warn("mission narrator request failed: {}", SDL_GetError());
}

std::size_t MissionNarrator::Open() {
  dialogs_.push_back({++token_, {}});
  Send(true);
  return token_;
}

void MissionNarrator::SetText(std::size_t token, std::string text) {
  const auto it = std::find_if(
      dialogs_.begin(), dialogs_.end(), [token](const auto &dialog) {
        return dialog.token == token;
      });
  if (it == dialogs_.end() || it->text == text)
    return;
  it->text = std::move(text);
  if (it == dialogs_.end() - 1)
    Send();
}

void MissionNarrator::Close(std::size_t token) {
  const bool current = !dialogs_.empty() && dialogs_.back().token == token;
  std::erase_if(dialogs_,
                [token](const auto &dialog) { return dialog.token == token; });
  if (current)
    Send();
}

bool MissionNarrator::HandleEvent(SdlPlatform &platform,
                                  const SDL_Event &event) {
  if (!active())
    return false;
  int action = -1;
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_F6)
      action = 0;
    if (event.key.key == SDLK_F8)
      action = 1;
    if (event.key.key == SDLK_F7)
      action = 2;
  } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
             event.button.button == SDL_BUTTON_LEFT) {
    const auto controls = Controls(platform);
    for (int i = 0; i < 3; ++i) {
      const SDL_FPoint point{event.button.x, event.button.y};
      if (SDL_PointInRectFloat(&point,
                               &controls[static_cast<std::size_t>(i)].second))
        action = i;
    }
  }
  if (action < 0)
    return false;
  if (action == 2) {
    SetEnabled(!enabled_);
    return true;
  }
  Send(action == 1);
  return true;
}

void MissionNarrator::AddControls(
    SdlPlatform &platform,
    std::vector<std::pair<std::string, SDL_FRect>> &rects) const {
  if (active()) {
    const auto controls = Controls(platform);
    rects.insert(rects.end(), controls.begin(), controls.end());
  }
}

void MissionNarrator::Draw(SdlPlatform &platform) {
  if (!active())
    return;
  if (!fonts_)
    fonts_ = std::make_unique<NovaFontCache>();
  const SdlPlatform::ScopedPlacement placement(
      platform, PlaceWindow(platform.logical_playfield_size()));
  auto *renderer = platform.renderer();
  const auto controls = Controls(platform);
  const float scale = controls[0].second.h / 30.0F;
  const char *labels[] = {"Read again (F6)",
                          "Stop (F8)",
                          enabled_ ? "Voice: On (F7)" : "Voice: Off (F7)"};
  for (std::size_t i = 0; i < controls.size(); ++i) {
    const auto &rect = controls[i].second;
    SDL_SetRenderDrawColor(renderer, 12, 32, 48, 255);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 85, 150, 170, 255);
    SDL_RenderRect(renderer, &rect);
    NovaText_DrawCentered(platform,
                          *fonts_,
                          NovaFontFamily::kGeneva,
                          12.0F * scale,
                          kNovaFontStyleRegular,
                          {220, 240, 245, 255},
                          rect.x,
                          rect.x + rect.w,
                          rect.y + 20 * scale,
                          labels[i]);
  }
}

std::string MissionNarrator::Snapshot() const {
  std::ifstream in(state_);
  const std::string worker{std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>()};
  return fmt::format(
      "{{\"enabled\":{},\"active\":{},\"voice\":\"{}\",\"installed\":[{},{},{},"
      "{}],\"installing\":{},\"worker\":{}}}",
      enabled_,
      active(),
      voice_,
      VoiceAvailable(kNarrationVoices[0].id),
      VoiceAvailable(kNarrationVoices[1].id),
      VoiceAvailable(kNarrationVoices[2].id),
      VoiceAvailable(kNarrationVoices[3].id),
      installing(),
      worker.empty() ? "null" : worker);
}

void NovaNarration_RunVoiceSettings(
    SdlPlatform &platform,
    NovaFontCache &fonts,
    const std::function<void()> &render_background) {
  using namespace preferences_detail;
  auto &narrator = platform.narrator();
  narrator.Stop();
  SdlPlatform::ScopedPlacement placement_guard(platform,
                                               platform.current_placement());
  ProbeUiAutoClear probe_guard(platform);
  constexpr SDL_FRect panel{0, 0, 480, 396};
  constexpr SDL_FRect install_button{144, 296, 192, 30};
  constexpr std::array<SDL_FRect, 4> buttons{{{24, 344, 90, 30},
                                              {122, 344, 70, 30},
                                              {200, 344, 144, 30},
                                              {352, 344, 104, 30}}};
  std::array<bool, kNarrationVoices.size()> available{};
  for (std::size_t i = 0; i < available.size(); ++i) {
    available[i] = narrator.VoiceAvailable(kNarrationVoices[i].id);
  }
  std::uint64_t next_poll = 0;
  while (!platform.quit_requested()) {
    if (SDL_GetTicks() >= next_poll) {
      narrator.PollInstallation();
      for (std::size_t i = 0; i < available.size(); ++i)
        available[i] = narrator.VoiceAvailable(kNarrationVoices[i].id);
      next_poll = SDL_GetTicks() + 250;
    }
    DrawOwningScreen(platform, render_background);
    platform.SetPlacement(PlaceContained({panel.w, panel.h},
                                         platform.logical_playfield_size(),
                                         platform.ui_scale()));
    auto *renderer = platform.renderer();
    DrawPanel(renderer, panel);
    NovaText_DrawCentered(platform,
                          fonts,
                          NovaFontFamily::kChicago,
                          18,
                          kNovaFontStyleRegular,
                          kControlText,
                          0,
                          panel.w,
                          30,
                          "Narration voice");
    NovaText_DrawCentered(platform,
                          fonts,
                          NovaFontFamily::kGeneva,
                          12,
                          kNovaFontStyleRegular,
                          kControlText,
                          0,
                          panel.w,
                          54,
                          "Used for missions and ship hails.");
    std::vector<std::pair<std::string, SDL_FRect>> controls;
    for (std::size_t i = 0; i < kNarrationVoices.size(); ++i) {
      const auto &voice = kNarrationVoices[i];
      const SDL_FRect row{24, 72 + static_cast<float>(i) * 48, 432, 40};
      const bool selected = narrator.voice() == voice.id;
      SDL_SetRenderDrawColor(renderer,
                             selected ? 80 : 35,
                             selected ? 22 : 37,
                             selected ? 28 : 42,
                             255);
      SDL_RenderFillRect(renderer, &row);
      const SDL_Color text_color = !available[i] ? kControlDisabledText
                                   : selected    ? kControlSelectedText
                                                 : kControlText;
      NovaText_Draw(platform,
                    fonts,
                    NovaFontFamily::kGeneva,
                    16,
                    kNovaFontStyleRegular,
                    text_color,
                    row.x + 14,
                    row.y + 26,
                    voice.name);
      NovaText_Draw(platform,
                    fonts,
                    NovaFontFamily::kGeneva,
                    12,
                    kNovaFontStyleRegular,
                    text_color,
                    row.x + 168,
                    row.y + 25,
                    available[i] ? voice.language : "Not installed");
      controls.emplace_back("voice." + std::string(voice.id),
                            platform.current_placement().ToWindowRect(row));
    }
    const std::array<std::string_view, 4> labels{
        "Preview",
        "Stop",
        narrator.enabled() ? "Auto-read: On" : "Auto-read: Off",
        "Done"};
    constexpr std::array<std::string_view, 4> names{
        "voice.preview", "voice.stop", "voice.enabled", "ok"};
    for (std::size_t i = 0; i < buttons.size(); ++i) {
      DrawButton(platform, fonts, buttons[i], labels[i], false);
      controls.emplace_back(
          names[i], platform.current_placement().ToWindowRect(buttons[i]));
    }
    const bool all_installed = std::all_of(
        available.begin(), available.end(), [](bool ready) { return ready; });
    const std::string_view status =
        narrator.install_status().empty()
            ? (all_installed ? "All four voices are installed."
                             : "Install four local voices (about 320 MB).")
            : std::string_view{narrator.install_status()};
    NovaText_DrawCentered(platform,
                          fonts,
                          NovaFontFamily::kGeneva,
                          11,
                          kNovaFontStyleRegular,
                          kControlText,
                          12,
                          468,
                          280,
                          std::string{status.substr(0, 72)});
    DrawButton(platform,
               fonts,
               install_button,
               narrator.installing() ? "Installing..."
               : all_installed       ? "Repair voices"
                                     : "Install voices",
               false);
    controls.emplace_back(
        "voice.install",
        platform.current_placement().ToWindowRect(install_button));
    platform.PublishProbeUi("narration_settings", std::move(controls));
    platform.Present();
    for (auto input = platform.PollTextEvent(); input;
         input = platform.PollTextEvent()) {
      if (input->key == TextKey::escape || input->key == TextKey::enter) {
        narrator.Stop();
        return;
      }
      if (input->key != TextKey::primary) {
        continue;
      }
      const auto click = platform.mouse_position();
      for (std::size_t i = 0; i < kNarrationVoices.size(); ++i) {
        const SDL_FRect row{24, 72 + static_cast<float>(i) * 48, 432, 40};
        if (available[i] && SDL_PointInRectFloat(&click, &row)) {
          (void)narrator.SelectVoice(kNarrationVoices[i].id);
        }
      }
      if (SDL_PointInRectFloat(&click, &install_button) &&
          !narrator.installing())
        (void)narrator.InstallVoices();
      if (SDL_PointInRectFloat(&click, &buttons[0]))
        narrator.PreviewVoice();
      if (SDL_PointInRectFloat(&click, &buttons[1]))
        narrator.Stop();
      if (SDL_PointInRectFloat(&click, &buttons[2]))
        narrator.SetEnabled(!narrator.enabled());
      if (SDL_PointInRectFloat(&click, &buttons[3])) {
        narrator.Stop();
        return;
      }
    }
    SDL_Delay(10);
  }
  narrator.Stop();
}

MissionNarrationScope::MissionNarrationScope(SdlPlatform &platform,
                                             bool active) {
  if (active) {
    narrator_ = &platform.narrator();
    token_ = narrator_->Open();
  }
}

MissionNarrationScope::~MissionNarrationScope() {
  if (narrator_)
    narrator_->Close(token_);
}

void MissionNarrationScope::SetText(std::string text) {
  if (narrator_)
    narrator_->SetText(token_, std::move(text));
}
} // namespace game
