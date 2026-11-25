#pragma once
#include <ArduinoJson.h>
#include "ArtworkMath.h"
#include "Artwork.h"
#include "Music.h"
#include <math.h>
namespace MusicMetadata {
inline void filter(JsonDocument &filter) {
    filter["player"]["trackState"] = true;
    filter["player"]["videoProgress"] = true;
    filter["player"]["adPlaying"] = true;
    filter["video"]["durationSeconds"] = true;
    filter["video"]["isLive"] = true;
    filter["video"]["title"] = true;

    filter["video"]["author"] = true;
    filter["video"]["id"] = true;
    filter["video"]["thumbnails"][0]["url"] = true;
    filter["video"]["thumbnails"][0]["width"] = true;
    filter["video"]["thumbnails"][0]["height"] = true;
}
inline void progress(JsonVariantConst player, JsonVariantConst video, Music::Snapshot &out, uint32_t now) {
  out.progressAvailable = false;
  out.positionMs = out.durationMs = 0;
  out.sampledAt = now;
  out.adPlaying = player["adPlaying"] | false;
  if (out.adPlaying || (video["isLive"] | false) ||
      !player["videoProgress"].is<double>() || !video["durationSeconds"].is<double>()) return;
  const double position = player["videoProgress"].as<double>();
  const double duration = video["durationSeconds"].as<double>();
  // Limit to seven days so conversion to milliseconds is always bounded.
  if (!isfinite(position) || !isfinite(duration) || position < 0 ||
      duration < 1 || duration > 604800) return;
  out.durationMs = uint32_t(duration * 1000);
  out.positionMs = position >= duration ? out.durationMs : uint32_t(position * 1000);
  out.progressAvailable = true;
}
inline const char *thumbnail(JsonVariantConst video) {
  const char *best = "";
  int bestScore = INT32_MAX;
  for (JsonObjectConst image : video["thumbnails"].as<JsonArrayConst>()) {
    const char *url = image["url"] | "";
    if (!ArtworkMath::allowedUrl(url) || strlen(url) >= 768) continue;
    const int width = image["width"] | 0, height = image["height"] | 0;
    if (width < 0 || height < 0 || width > 1024 || height > 1024) continue;
    const int side = min(width, height);
    const int score = side >= MusicArtwork::SIDE ? side-MusicArtwork::SIDE :
                      10000+MusicArtwork::SIDE-side;
    if (score < bestScore) { best = url; bestScore = score; }
  }
  return best;
}

}
