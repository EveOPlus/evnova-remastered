#include "movie_player.hpp"
#include "../brgr_archive.hpp"
#include "../log.hpp"
#include "../sdl_platform.hpp"
#include "nova_font.hpp"
#include <algorithm>
#include <memory>

// Third-party implementation is isolated from application diagnostics.
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define PL_MPEG_IMPLEMENTATION
#include <pl_mpeg.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace game {
namespace {
struct Movie {
  std::unique_ptr<plm_t, decltype(&plm_destroy)> decoder{nullptr, plm_destroy};
  std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture{
      nullptr, SDL_DestroyTexture};
  std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> audio{
      nullptr, SDL_DestroyAudioStream};
  bool frame_ready = false;
};

void VideoFrame(plm_t *, plm_frame_t *frame, void *user) {
  auto &movie = *static_cast<Movie *>(user);
  movie.frame_ready = SDL_UpdateYUVTexture(movie.texture.get(),
                                           nullptr,
                                           frame->y.data,
                                           static_cast<int>(frame->y.width),
                                           frame->cb.data,
                                           static_cast<int>(frame->cb.width),
                                           frame->cr.data,
                                           static_cast<int>(frame->cr.width));
}

void AudioFrame(plm_t *, plm_samples_t *samples, void *user) {
  auto &movie = *static_cast<Movie *>(user);
  if (movie.audio)
    SDL_PutAudioStreamData(
        movie.audio.get(),
        samples->interleaved,
        static_cast<int>(samples->count * 2 * sizeof(float)));
}
} // namespace

// Ghidra 0x0049db00 Ui_PlayMovieFileModal: named asset, centered playback,
// scaled to fit, input to dismiss. SDL/MPEG replaces the QuickTime platform.
bool NovaMovie_Play(SdlPlatform &platform, const std::string &name) {
  if (name.empty() || !platform.movies_enabled())
    return false;
  const auto path = NovaResource_LocateFile(name + ".mpg");
  if (!path)
    return false;
  Movie movie;
  movie.decoder.reset(plm_create_with_filename(path->string().c_str()));
  if (!movie.decoder || !plm_has_headers(movie.decoder.get()) ||
      plm_get_num_video_streams(movie.decoder.get()) == 0) {
    NovaLog::Warn("cannot decode movie '{}'", path->string());
    return false;
  }
  const int width = plm_get_width(movie.decoder.get());
  const int height = plm_get_height(movie.decoder.get());
  movie.texture.reset(SDL_CreateTexture(platform.renderer(),
                                        SDL_PIXELFORMAT_IYUV,
                                        SDL_TEXTUREACCESS_STREAMING,
                                        width,
                                        height));
  if (!movie.texture)
    return false;
  if (plm_get_num_audio_streams(movie.decoder.get()) > 0) {
    const SDL_AudioSpec spec{
        SDL_AUDIO_F32, 2, plm_get_samplerate(movie.decoder.get())};
    movie.audio.reset(SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
    if (movie.audio)
      SDL_ResumeAudioStreamDevice(movie.audio.get());
  }
  plm_set_video_decode_callback(movie.decoder.get(), VideoFrame, &movie);
  plm_set_audio_enabled(movie.decoder.get(), movie.audio != nullptr);
  plm_set_audio_decode_callback(movie.decoder.get(), AudioFrame, &movie);
  const SdlPlatform::ScopedPlacement restore(platform,
                                             platform.current_placement());
  ProbeUiAutoClear probe_ui_guard(platform);
  NovaFontCache fonts;
  auto previous = SDL_GetTicks();
  NovaLog::Info("playing movie '{}' ({} x {})", name, width, height);
  bool skipped = false;
  while (!platform.quit_requested() && !plm_has_ended(movie.decoder.get()) &&
         !skipped) {
    const auto now = SDL_GetTicks();
    plm_decode(movie.decoder.get(), std::min((now - previous) / 1000.0, 0.1));
    previous = now;
    platform.SetPlacement(PlaceWindow(platform.logical_playfield_size()));
    const auto size = platform.logical_playfield_size();
    const float scale = std::min(size.x / width, (size.y - 48) / height);
    const SDL_FRect dest{(size.x - width * scale) / 2,
                         (size.y - 48 - height * scale) / 2,
                         width * scale,
                         height * scale};
    SDL_SetRenderDrawColor(platform.renderer(), 0, 0, 0, 255);
    SDL_RenderClear(platform.renderer());
    if (movie.frame_ready)
      SDL_RenderTexture(
          platform.renderer(), movie.texture.get(), nullptr, &dest);
    NovaText_DrawCentered(platform,
                          fonts,
                          NovaFontFamily::kGeneva,
                          16,
                          kNovaFontStyleRegular,
                          {230, 230, 230, 255},
                          0,
                          size.x,
                          size.y - 16,
                          "Click or press Escape to continue");
    platform.PublishProbeUi("movie", {{"skip", {0, 0, size.x, size.y}}});
    platform.Present();
    while (const auto in = platform.PollTextEvent()) {
      if (in->key == TextKey::escape || in->key == TextKey::enter ||
          in->key == TextKey::primary || in->key == TextKey::character)
        skipped = true;
    }
    SDL_Delay(5);
  }
  NovaLog::Info("movie '{}' {}", name, skipped ? "skipped" : "finished");
  return true;
}
} // namespace game
