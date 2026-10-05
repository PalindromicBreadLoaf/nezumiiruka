// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/UpdaterSwitch.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <switch.h>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <mbedtls/sha256.h>
#include <picojson.h>

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/HttpRequest.h"
#include "Common/IOFile.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"

namespace UpdaterSwitch
{
namespace
{
constexpr const char* RELEASES_URL =
    "https://api.github.com/repos/PalindromicBreadLoaf/porpoise/releases?per_page=20";
constexpr const char* USER_AGENT = "Porpoise-Updater/" PORPOISE_VERSION;
constexpr std::string_view ASSET_NAME = "porpoise.nro";
constexpr std::string_view DIGEST_PREFIX = "sha256:";
constexpr size_t NRO_MAGIC_OFFSET = 0x10;
constexpr std::string_view NRO_MAGIC = "NRO0";

std::string s_executable_path;

using VersionPart = std::variant<u64, std::string>;

struct ParsedVersion
{
  std::array<u64, 3> core{};
  std::vector<VersionPart> prerelease;
};

std::string Normalise(std::string_view version)
{
  version = StripWhitespace(version);
  if (!version.empty() && (version.front() == 'v' || version.front() == 'V'))
    version.remove_prefix(1);

  std::string result(version);
  Common::ToLower(&result);
  return result;
}

std::optional<u64> ParseNumber(std::string_view text)
{
  if (text.empty() || !std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; }))
    return std::nullopt;

  u64 value;
  if (!TryParse(std::string(text), &value, 10))
    return std::nullopt;
  return value;
}

std::optional<ParsedVersion> ParseVersion(std::string_view raw)
{
  const std::string version = Normalise(raw);
  const size_t dash = version.find('-');
  const std::string_view core = std::string_view(version).substr(0, dash);

  ParsedVersion parsed;
  const std::vector<std::string> components = SplitString(std::string(core), '.');
  if (components.empty() || components.size() > parsed.core.size())
    return std::nullopt;
  for (size_t i = 0; i < components.size(); ++i)
  {
    const std::optional<u64> number = ParseNumber(components[i]);
    if (!number)
      return std::nullopt;
    parsed.core[i] = *number;
  }

  if (dash == std::string::npos)
    return parsed;

  const std::string_view suffix = std::string_view(version).substr(dash + 1);
  const auto is_separator = [](char c) { return c == '.' || c == '-' || c == '_'; };
  const auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
  size_t pos = 0;
  while (pos < suffix.size())
  {
    if (is_separator(suffix[pos]))
    {
      ++pos;
      continue;
    }

    const bool numeric = is_digit(suffix[pos]);
    const size_t begin = pos;
    while (pos < suffix.size() && !is_separator(suffix[pos]) && is_digit(suffix[pos]) == numeric)
      ++pos;

    const std::string_view token = suffix.substr(begin, pos - begin);
    if (numeric)
    {
      const std::optional<u64> number = ParseNumber(token);
      if (!number)
        return std::nullopt;
      parsed.prerelease.emplace_back(*number);
    }
    else
    {
      parsed.prerelease.emplace_back(std::string(token));
    }
  }

  if (parsed.prerelease.empty())
    return std::nullopt;
  return parsed;
}

int Compare(const ParsedVersion& lhs, const ParsedVersion& rhs)
{
  if (lhs.core != rhs.core)
    return lhs.core < rhs.core ? -1 : 1;

  if (lhs.prerelease.empty() != rhs.prerelease.empty())
    return lhs.prerelease.empty() ? 1 : -1;

  if (lhs.prerelease != rhs.prerelease)
    return lhs.prerelease < rhs.prerelease ? -1 : 1;
  return 0;
}

std::string GetString(const picojson::object& object, const std::string& key)
{
  const auto it = object.find(key);
  return it != object.end() && it->second.is<std::string>() ? it->second.get<std::string>() :
                                                              std::string();
}

bool GetBool(const picojson::object& object, const std::string& key, bool fallback)
{
  const auto it = object.find(key);
  return it != object.end() && it->second.is<bool>() ? it->second.get<bool>() : fallback;
}

u64 GetSize(const picojson::object& object, const std::string& key)
{
  const auto it = object.find(key);
  if (it == object.end() || !it->second.is<double>() || it->second.get<double>() < 0)
    return 0;
  return static_cast<u64>(it->second.get<double>());
}

std::optional<Release> ParseRelease(const picojson::value& value)
{
  if (!value.is<picojson::object>())
    return std::nullopt;

  const picojson::object& object = value.get<picojson::object>();
  if (GetBool(object, "draft", true))
    return std::nullopt;

  Release release;
  release.tag = GetString(object, "tag_name");
  release.notes = GetString(object, "body");
  release.prerelease = GetBool(object, "prerelease", false);
  if (release.tag.empty())
    return std::nullopt;

  const auto assets = object.find("assets");
  if (assets == object.end() || !assets->second.is<picojson::array>())
    return std::nullopt;

  for (const picojson::value& asset_value : assets->second.get<picojson::array>())
  {
    if (!asset_value.is<picojson::object>())
      continue;

    const picojson::object& asset = asset_value.get<picojson::object>();
    if (GetString(asset, "name") != ASSET_NAME)
      continue;

    release.download_url = GetString(asset, "browser_download_url");
    release.size = GetSize(asset, "size");
    const std::string digest = GetString(asset, "digest");
    if (digest.starts_with(DIGEST_PREFIX))
    {
      release.sha256 = digest.substr(DIGEST_PREFIX.size());
      Common::ToLower(&release.sha256);
    }
    break;
  }

  if (release.download_url.empty() || release.size == 0 || release.sha256.size() != 64)
    return std::nullopt;
  return release;
}

Common::HttpRequest::Headers GetHeaders()
{
  return {{"User-Agent", USER_AGENT}};
}

Common::HttpRequest::ProgressCallback CancelWith(const std::atomic_bool& cancelled,
                                                 ProgressFn progress = {})
{
  return [&cancelled, progress = std::move(progress)](s64 total, s64 now, s64, s64) {
    if (progress)
      progress(static_cast<u64>(std::max<s64>(now, 0)), static_cast<u64>(std::max<s64>(total, 0)));
    return !cancelled.load();
  };
}

std::string HexDigest(const std::vector<u8>& data)
{
  std::array<u8, 32> digest;
  mbedtls_sha256_ret(data.data(), data.size(), digest.data(), 0);
  return fmt::format("{:02x}", fmt::join(digest, ""));
}

bool IsNro(const std::vector<u8>& data)
{
  return data.size() >= NRO_MAGIC_OFFSET + NRO_MAGIC.size() &&
         std::memcmp(data.data() + NRO_MAGIC_OFFSET, NRO_MAGIC.data(), NRO_MAGIC.size()) == 0;
}

std::string GetNotesPath()
{
  return File::GetUserPath(D_CACHE_IDX) + "ReleaseNotes.txt";
}
}  // namespace

const char* GetCurrentVersion()
{
  return PORPOISE_VERSION;
}

void SetExecutablePath(std::string path)
{
  s_executable_path = std::move(path);
}

const std::string& GetExecutablePath()
{
  return s_executable_path;
}

int CompareVersions(std::string_view lhs, std::string_view rhs)
{
  const std::optional<ParsedVersion> a = ParseVersion(lhs);
  const std::optional<ParsedVersion> b = ParseVersion(rhs);
  if (a && b)
    return Compare(*a, *b);

  const std::string normalised_a = Normalise(lhs);
  const std::string normalised_b = Normalise(rhs);
  if (normalised_a == normalised_b)
    return 0;
  return normalised_a < normalised_b ? -1 : 1;
}

CheckResult CheckForUpdate(bool include_prereleases, const std::atomic_bool& cancelled)
{
  CheckResult result;

  Common::HttpRequest request(std::chrono::seconds(15), CancelWith(cancelled));
  request.FollowRedirects(5);
  const Common::HttpRequest::Response response = request.Get(RELEASES_URL, GetHeaders());
  if (!response)
  {
    if (cancelled)
      result.error = "The check was cancelled.";
    else if (const s32 code = request.GetLastResponseCode(); code != 0)
      result.error = fmt::format("GitHub replied with HTTP {}.", code);
    else
      result.error = "Could not reach GitHub. Check your internet connection.";
    WARN_LOG_FMT(COMMON, "Update check failed: {}", result.error);
    return result;
  }

  picojson::value releases;
  const std::string parse_error = picojson::parse(
      releases, std::string(reinterpret_cast<const char*>(response->data()), response->size()));
  if (!parse_error.empty() || !releases.is<picojson::array>())
  {
    result.error = "GitHub returned unreadable release data.";
    WARN_LOG_FMT(COMMON, "Update check failed: {}", parse_error);
    return result;
  }

  std::optional<Release> newest;
  for (const picojson::value& value : releases.get<picojson::array>())
  {
    std::optional<Release> release = ParseRelease(value);
    if (!release)
      continue;

    if (result.current_notes.empty() && CompareVersions(release->tag, GetCurrentVersion()) == 0)
      result.current_notes = release->notes;

    if (release->prerelease && !include_prereleases)
      continue;

    if (!newest || CompareVersions(release->tag, newest->tag) > 0)
      newest = std::move(release);
  }

  if (!newest)
  {
    result.error = "GitHub lists no release of Porpoise that can be installed.";
    return result;
  }

  result.status = CompareVersions(newest->tag, GetCurrentVersion()) > 0 ? CheckStatus::Available :
                                                                          CheckStatus::UpToDate;
  result.release = std::move(*newest);
  return result;
}

std::expected<void, std::string> Install(const Release& release, const std::atomic_bool& cancelled,
                                         const ProgressFn& progress)
{
  const std::string& path = s_executable_path;
  if (!path.ends_with(".nro") || !File::IsFile(path))
    return std::unexpected("Could not find the running copy of Porpoise.");

  Common::HttpRequest request(std::chrono::seconds(30), CancelWith(cancelled, progress));
  request.FollowRedirects(5);
  const Common::HttpRequest::Response data = request.Get(release.download_url, GetHeaders());
  if (cancelled)
    return std::unexpected("The update was cancelled.");
  if (!data)
    return std::unexpected("The download failed. Check your internet connection.");

  if (data->size() != release.size)
    return std::unexpected("The download is incomplete.");
  if (HexDigest(*data) != release.sha256)
    return std::unexpected("The download is corrupt.");
  if (!IsNro(*data))
    return std::unexpected("The download is not a Switch homebrew application.");

  const std::string temporary = path + ".update";
  const std::string backup = path + ".backup";

  {
    File::IOFile file(temporary, "wb");
    if (!file || !file.WriteBytes(data->data(), data->size()) || !file.Close())
    {
      File::Delete(temporary);
      return std::unexpected("Could not write the update to the SD card.");
    }
  }

  File::Delete(backup);
  if (!File::Rename(path, backup))
  {
    File::Delete(temporary);
    return std::unexpected("Could not back up the current version.");
  }

  if (!File::Rename(temporary, path))
  {
    if (File::Rename(backup, path))
      return std::unexpected("Could not install the update. The current version was restored.");
    return std::unexpected("Could not install the update.");
  }

  NOTICE_LOG_FMT(COMMON, "Updated Porpoise {} to {}", GetCurrentVersion(), release.tag);
  return {};
}

bool CanRelaunch()
{
  return !s_executable_path.empty() && envHasNextLoad();
}

void QueueRelaunch()
{
  const std::string argv = fmt::format("\"{}\"", s_executable_path);
  envSetNextLoad(s_executable_path.c_str(), argv.c_str());
}

ReleaseNotes LoadCachedNotes()
{
  std::string contents;
  if (!File::ReadFileToString(GetNotesPath(), contents))
    return {};

  const size_t newline = contents.find('\n');
  if (newline == std::string::npos)
    return {};

  return {std::string(StripWhitespace(std::string_view(contents).substr(0, newline))),
          contents.substr(newline + 1)};
}

void CacheNotes(const std::string& tag, const std::string& text)
{
  if (tag.empty())
    return;

  const ReleaseNotes cached = LoadCachedNotes();
  if (cached.tag == tag && cached.text == text)
    return;

  File::CreateFullPath(GetNotesPath());
  File::WriteStringToFile(GetNotesPath(), tag + '\n' + text);
}
}  // namespace UpdaterSwitch
