// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/ShellAchievementsSwitch.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <switch.h>

#include <borealis.hpp>
#include <borealis/views/cells/cell_bool.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/cells/cell_input.hpp>
#include <fmt/format.h>
#include <rcheevos/include/rc_api_runtime.h>
#include <rcheevos/include/rc_api_user.h>
#include <rcheevos/include/rc_error.h>

#include "Common/CommonPaths.h"
#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/HookableEvent.h"
#include "Common/HttpRequest.h"
#include "Common/IOFile.h"
#include "Common/ScopeGuard.h"
#include "Common/Version.h"
#include "Core/AchievementManager.h"
#include "Core/Config/AchievementSettings.h"

namespace Shell
{
namespace
{
constexpr std::chrono::seconds REQUEST_TIMEOUT{10};
constexpr float BADGE_SIZE = 64;

NVGcolor ThemeColour(const std::string& name)
{
  return brls::Application::getTheme()[name];
}

std::string DescribeLoginFailure(int code)
{
  switch (code)
  {
  case RC_LOGIN_REQUIRED:
  case RC_EXPIRED_TOKEN:
    return "The saved login has expired";
  case RC_INVALID_CREDENTIALS:
    return "Wrong username or password";
  case RC_NO_RESPONSE:
    return "No internet connection";
  default:
    return "Server error";
  }
}

std::optional<std::string> AskForPassword()
{
  SwkbdConfig config;
  if (R_FAILED(swkbdCreate(&config, 0)))
    return std::nullopt;

  swkbdConfigMakePresetPassword(&config);
  swkbdConfigSetHeaderText(&config, "RetroAchievements password");
  swkbdConfigSetStringLenMax(&config, 128);

  char buffer[0x100] = {};
  const Result result = swkbdShow(&config, buffer, sizeof(buffer));
  swkbdClose(&config);

  if (R_FAILED(result) || buffer[0] == '\0')
    return std::nullopt;
  return std::string(buffer);
}

class AccountView final : public brls::Box
{
public:
  AccountView() : brls::Box(brls::Axis::COLUMN)
  {
    m_enabled = new brls::BooleanCell();
    m_enabled->init("Enable RetroAchievements", Config::Get(Config::RA_ENABLED),
                    [this](bool enabled) { SetEnabled(enabled); });
    addView(m_enabled);

    m_status = new brls::DetailCell();
    m_status->setText("Account");
    addView(m_status);

    m_username = new brls::InputCell();
    m_username->init(
        "Username", Config::Get(Config::RA_USERNAME),
        [](std::string username) { Config::SetBase(Config::RA_USERNAME, username); }, "Not set",
        "RetroAchievements username", 64);
    addView(m_username);

    m_login = new brls::DetailCell();
    m_login->setText("Log in");
    m_login->registerClickAction([this](brls::View*) {
      Login();
      return true;
    });
    addView(m_login);

    m_logout = new brls::DetailCell();
    m_logout->setText("Log out");
    m_logout->registerClickAction([this](brls::View*) {
      ConfirmLogout();
      return true;
    });
    addView(m_logout);

    m_login_hook =
        AchievementManager::GetInstance().login_event.Register([this, alive = m_alive](int result) {
          brls::sync([this, alive, result] {
            if (*alive)
              OnLoginFinished(result);
          });
        });

    Refresh();
  }

  ~AccountView() override { *m_alive = false; }

private:
  void SetEnabled(bool enabled)
  {
    Config::SetBase(Config::RA_ENABLED, enabled);

    auto& manager = AchievementManager::GetInstance();
    if (enabled)
    {
      manager.Init(nullptr);
      m_logging_in = manager.HasAPIToken();
    }
    else
    {
      manager.Shutdown();
      m_logging_in = false;
    }

    m_failure.clear();
    Refresh();
  }

  void Login()
  {
    if (Config::Get(Config::RA_USERNAME).empty())
    {
      brls::Application::notify("Enter your username first.");
      return;
    }

    const std::optional<std::string> password = AskForPassword();
    if (!password)
      return;

    m_logging_in = true;
    m_failure.clear();
    Refresh();

    AchievementManager::GetInstance().Login(*password);
  }

  void ConfirmLogout()
  {
    auto* dialog = new brls::Dialog("Log out of RetroAchievements?");
    dialog->addButton("Cancel", [] {});
    dialog->addButton("Log out", [this, alive = m_alive] {
      if (!*alive)
        return;

      AchievementManager::GetInstance().Logout();
      Config::Save();
      m_failure.clear();
      Refresh();
    });
    dialog->open();
  }

  void OnLoginFinished(int result)
  {
    m_logging_in = false;
    if (result == RC_OK)
    {
      m_failure.clear();
      Config::Save();
      brls::Application::notify(fmt::format("Logged in as {}.", GetDisplayName()));
    }
    else
    {
      m_failure = DescribeLoginFailure(result);
    }
    Refresh();
  }

  static std::string GetDisplayName()
  {
    const std::string name(AchievementManager::GetInstance().GetPlayerDisplayName());
    return name.empty() ? Config::Get(Config::RA_USERNAME) : name;
  }

  void Refresh()
  {
    auto& manager = AchievementManager::GetInstance();
    const bool enabled = Config::Get(Config::RA_ENABLED);
    const bool logged_in = enabled && manager.HasAPIToken();

    std::string status;
    if (!enabled)
      status = "Disabled";
    else if (m_logging_in)
      status = "Logging in...";
    else if (!logged_in)
      status = m_failure.empty() ? std::string("Not logged in") : m_failure;
    else if (const u32 score = manager.GetPlayerScore(); score != 0)
      status = fmt::format("{} ({} points)", GetDisplayName(), score);
    else
      status = GetDisplayName();
    m_status->setDetailText(status);

    const auto shown = [](bool visible) {
      return visible ? brls::Visibility::VISIBLE : brls::Visibility::GONE;
    };
    const bool can_log_in = enabled && !logged_in && !m_logging_in;
    m_username->setVisibility(shown(can_log_in));
    m_login->setVisibility(shown(can_log_in));
    m_logout->setVisibility(shown(logged_in && !m_logging_in));

    brls::View* focus = brls::Application::getCurrentFocus();
    if (focus && focus->getVisibility() == brls::Visibility::GONE)
      brls::Application::giveFocus(m_status);
  }

  brls::BooleanCell* m_enabled;
  brls::DetailCell* m_status;
  brls::InputCell* m_username;
  brls::DetailCell* m_login;
  brls::DetailCell* m_logout;

  bool m_logging_in = false;
  std::string m_failure;

  std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
  Common::EventHook m_login_hook;
};

struct Achievement
{
  std::string title;
  std::string description;
  u32 points = 0;
  bool unofficial = false;
  bool unlocked = false;
  bool hardcore = false;
  std::string badge_url;
};

struct AchievementSet
{
  std::string title;
  std::vector<Achievement> achievements;
};

struct GameAchievements
{
  std::string error;
  std::vector<AchievementSet> sets;
};

std::string GetBadgeCachePath(std::string_view url)
{
  const size_t name_start = url.rfind('/');
  if (name_start == std::string_view::npos || name_start == 0 || name_start + 1 == url.size())
    return {};

  const size_t folder_start = url.rfind('/', name_start - 1);
  const std::string_view folder = url.substr(folder_start + 1, name_start - folder_start - 1);
  const std::string_view name = url.substr(name_start + 1);
  return fmt::format("{}RetroAchievements" DIR_SEP "{}_{}", File::GetUserPath(D_CACHE_IDX), folder,
                     name);
}

Common::HttpRequest MakeHttpRequest(std::stop_token stop)
{
  return Common::HttpRequest(REQUEST_TIMEOUT,
                             [stop](s64, s64, s64, s64) { return !stop.stop_requested(); });
}

const Common::HttpRequest::Headers& GetHeaders()
{
  static const Common::HttpRequest::Headers headers = {
      {"User-Agent", Common::GetUserAgentStr() + " (Switch)"}};
  return headers;
}

template <typename Response>
int SendApiRequest(rc_api_request_t& request, Response* response,
                   int (*process)(Response*, const rc_api_server_response_t*), std::stop_token stop)
{
  Common::ScopeGuard request_guard([&request] { rc_api_destroy_request(&request); });

  Common::HttpRequest http = MakeHttpRequest(stop);
  Common::HttpRequest::Response body;
  if (request.post_data && request.post_data[0] != '\0')
  {
    body = http.Post(request.url, std::string_view(request.post_data), GetHeaders(),
                     Common::HttpRequest::AllowedReturnCodes::All);
  }
  else
  {
    body = http.Get(request.url, GetHeaders(), Common::HttpRequest::AllowedReturnCodes::All);
  }

  rc_api_server_response_t server_response{};
  if (body && !body->empty())
  {
    server_response.body = reinterpret_cast<const char*>(body->data());
    server_response.body_length = body->size();
    server_response.http_status_code = http.GetLastResponseCode();
  }
  else
  {
    server_response.body = "";
    server_response.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
  }

  return process(response, &server_response);
}

bool FetchUnlocks(const std::string& username, const std::string& token, const rc_api_host_t* host,
                  u32 game_id, bool hardcore, std::unordered_set<u32>* unlocks,
                  std::stop_token stop)
{
  const rc_api_fetch_user_unlocks_request_t params{
      .username = username.c_str(),
      .api_token = token.c_str(),
      .game_id = game_id,
      .hardcore = hardcore ? 1u : 0u,
  };

  rc_api_request_t request;
  if (rc_api_init_fetch_user_unlocks_request_hosted(&request, &params, host) != RC_OK)
    return false;

  rc_api_fetch_user_unlocks_response_t response{};
  const int result =
      SendApiRequest(request, &response, rc_api_process_fetch_user_unlocks_server_response, stop);
  Common::ScopeGuard response_guard(
      [&response] { rc_api_destroy_fetch_user_unlocks_response(&response); });

  if (result != RC_OK || !response.response.succeeded)
    return false;

  unlocks->insert(response.achievement_ids,
                  response.achievement_ids + response.num_achievement_ids);
  return true;
}

std::mutex s_hash_mutex;

GameAchievements FetchGameAchievements(const std::string& path, std::stop_token stop)
{
  GameAchievements result;

  std::string hash;
  {
    std::lock_guard lock(s_hash_mutex);
    hash = AchievementManager::CalculateHash(path);
  }
  if (hash.size() != 32)
  {
    result.error = "This game could not be identified.";
    return result;
  }

  if (stop.stop_requested())
    return result;

  const std::string username = Config::Get(Config::RA_USERNAME);
  const std::string token = Config::Get(Config::RA_API_TOKEN);
  const std::string host_url = Config::Get(Config::RA_HOST_URL);
  const rc_api_host_t host{.host = host_url.c_str(), .media_host = nullptr};
  const rc_api_host_t* host_ptr = host_url.empty() ? nullptr : &host;

  const rc_api_fetch_game_sets_request_t params{
      .username = username.c_str(),
      .api_token = token.c_str(),
      .game_id = 0,
      .game_hash = hash.c_str(),
  };

  rc_api_request_t request;
  if (rc_api_init_fetch_game_sets_request_hosted(&request, &params, host_ptr) != RC_OK)
  {
    result.error = "Could not build the request.";
    return result;
  }

  rc_api_fetch_game_sets_response_t game{};
  const int game_result =
      SendApiRequest(request, &game, rc_api_process_fetch_game_sets_server_response, stop);
  Common::ScopeGuard game_guard([&game] { rc_api_destroy_fetch_game_sets_response(&game); });

  if (stop.stop_requested())
    return result;

  if (game_result != RC_OK || !game.response.succeeded)
  {
    if (game_result == RC_NO_RESPONSE)
      result.error = "Could not reach RetroAchievements.";
    else if (game.response.error_message)
      result.error = game.response.error_message;
    else
      result.error = "RetroAchievements could not find this game.";
    return result;
  }

  if (game.id == 0 || game.num_sets == 0)
  {
    result.error = "RetroAchievements does not recognise this version of the game.";
    return result;
  }

  std::set<u32> game_ids;
  for (u32 i = 0; i < game.num_sets; ++i)
    game_ids.insert(game.sets[i].game_id);

  std::unordered_set<u32> hardcore_unlocks;
  std::unordered_set<u32> softcore_unlocks;
  for (const u32 game_id : game_ids)
  {
    if (!FetchUnlocks(username, token, host_ptr, game_id, true, &hardcore_unlocks, stop) ||
        !FetchUnlocks(username, token, host_ptr, game_id, false, &softcore_unlocks, stop))
    {
      result.error = "Could not fetch your unlocked achievements.";
      return result;
    }
  }

  const bool show_unofficial = Config::Get(Config::RA_UNOFFICIAL_ENABLED);
  for (u32 i = 0; i < game.num_sets; ++i)
  {
    const rc_api_achievement_set_definition_t& definition = game.sets[i];

    AchievementSet set;
    set.title = definition.title ? definition.title : (game.title ? game.title : "");

    for (u32 j = 0; j < definition.num_achievements; ++j)
    {
      const rc_api_achievement_definition_t& source = definition.achievements[j];
      const bool unofficial = source.category == RC_ACHIEVEMENT_CATEGORY_UNOFFICIAL;
      if (unofficial && !show_unofficial)
        continue;

      Achievement achievement;
      achievement.title = source.title ? source.title : "";
      achievement.description = source.description ? source.description : "";
      achievement.points = source.points;
      achievement.unofficial = unofficial;
      achievement.hardcore = hardcore_unlocks.contains(source.id);
      achievement.unlocked = achievement.hardcore || softcore_unlocks.contains(source.id);

      const char* badge_url = achievement.unlocked ? source.badge_url : source.badge_locked_url;
      if (badge_url)
        achievement.badge_url = badge_url;

      set.achievements.push_back(std::move(achievement));
    }

    std::ranges::stable_partition(set.achievements, &Achievement::unlocked);

    if (!set.achievements.empty())
      result.sets.push_back(std::move(set));
  }

  return result;
}

std::optional<std::vector<u8>> LoadBadge(const std::string& url, std::stop_token stop)
{
  const std::string cache_path = GetBadgeCachePath(url);
  if (cache_path.empty())
    return std::nullopt;

  if (File::IOFile file(cache_path, "rb"); file)
  {
    std::vector<u8> data(file.GetSize());
    if (!data.empty() && file.ReadBytes(data.data(), data.size()))
      return data;
  }

  Common::HttpRequest http = MakeHttpRequest(stop);
  Common::HttpRequest::Response response = http.Get(url, GetHeaders());
  if (!response || response->empty())
    return std::nullopt;

  File::CreateFullPath(cache_path);
  File::IOFile(cache_path, "wb").WriteBytes(response->data(), response->size());
  return response;
}

struct FetchWorker
{
  std::jthread thread;
  std::shared_ptr<std::atomic_bool> done;
};

std::vector<FetchWorker> s_workers;

void StartWorker(std::function<void(std::stop_token)> work)
{
  std::erase_if(s_workers, [](const FetchWorker& worker) { return worker.done->load(); });

  auto done = std::make_shared<std::atomic_bool>(false);
  s_workers.push_back({std::jthread([work = std::move(work), done](std::stop_token stop) {
                         work(stop);
                         done->store(true);
                       }),
                       done});
}

class GameAchievementsView final : public brls::Box
{
public:
  explicit GameAchievementsView(std::string path) : brls::Box(brls::Axis::COLUMN)
  {
    m_summary = new brls::Label();
    m_summary->setText("Fetching achievements...");
    m_summary->setFontSize(18);
    m_summary->setMargins(8, 16, 16, 16);
    addView(m_summary);

    StartWorker([this, alive = m_alive, path = std::move(path)](std::stop_token stop) {
      GameAchievements achievements = FetchGameAchievements(path, stop);
      if (stop.stop_requested())
        return;

      std::vector<std::string> badge_urls;
      for (const AchievementSet& set : achievements.sets)
      {
        for (const Achievement& achievement : set.achievements)
          badge_urls.push_back(achievement.badge_url);
      }

      brls::sync([this, alive, achievements = std::move(achievements)] {
        if (*alive)
          Show(achievements);
      });

      for (size_t i = 0; i < badge_urls.size() && !stop.stop_requested(); ++i)
      {
        if (badge_urls[i].empty())
          continue;

        std::optional<std::vector<u8>> badge = LoadBadge(badge_urls[i], stop);
        if (!badge)
          continue;

        brls::sync([this, alive, i, badge = std::move(*badge)] {
          if (*alive && i < m_badges.size())
            m_badges[i]->setImageFromMem(badge.data(), static_cast<int>(badge.size()));
        });
      }
    });
    m_stop_source = s_workers.back().thread.get_stop_source();
  }

  ~GameAchievementsView() override
  {
    *m_alive = false;
    m_stop_source.request_stop();
  }

private:
  void Show(const GameAchievements& achievements)
  {
    if (!achievements.error.empty())
    {
      m_summary->setText(achievements.error);
      return;
    }

    u32 unlocked = 0;
    u32 total = 0;
    u32 unlocked_points = 0;
    u32 total_points = 0;
    for (const AchievementSet& set : achievements.sets)
    {
      for (const Achievement& achievement : set.achievements)
      {
        ++total;
        total_points += achievement.points;
        if (achievement.unlocked)
        {
          ++unlocked;
          unlocked_points += achievement.points;
        }
      }
    }

    if (total == 0)
    {
      m_summary->setText("This game has no achievements yet.");
      return;
    }

    m_summary->setText(fmt::format("{} of {} achievements unlocked, worth {} of {} points.",
                                   unlocked, total, unlocked_points, total_points));

    for (const AchievementSet& set : achievements.sets)
    {
      if (achievements.sets.size() > 1)
      {
        auto* header = new brls::Header();
        header->setTitle(set.title);
        header->setMarginTop(24);
        addView(header);
      }

      for (const Achievement& achievement : set.achievements)
        AddRow(achievement);
    }
  }

  void AddRow(const Achievement& achievement)
  {
    auto* row = new brls::Box(brls::Axis::ROW);
    row->setFocusable(true);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setPadding(12, 16, 12, 16);

    auto* badge = new brls::Image();
    badge->setDimensions(BADGE_SIZE, BADGE_SIZE);
    badge->setScalingType(brls::ImageScalingType::FIT);
    badge->setMarginRight(20);
    row->addView(badge);
    m_badges.push_back(badge);

    auto* text = new brls::Box(brls::Axis::COLUMN);
    text->setGrow(1.0f);
    text->setShrink(1.0f);
    row->addView(text);

    auto* title = new brls::Label();
    title->setText(achievement.title);
    title->setFontSize(20);
    title->setTextColor(ThemeColour(achievement.unlocked ? "brls/text" : "brls/text_disabled"));
    text->addView(title);

    if (!achievement.description.empty())
    {
      auto* description = new brls::Label();
      description->setText(achievement.description);
      description->setFontSize(16);
      description->setTextColor(ThemeColour("brls/header/subtitle"));
      description->setMarginTop(4);
      text->addView(description);
    }

    std::string state = achievement.points == 1 ? std::string("1 point") :
                                                  fmt::format("{} points", achievement.points);
    if (achievement.hardcore)
      state += " · Unlocked in hardcore";
    else if (achievement.unlocked)
      state += " · Unlocked";
    if (achievement.unofficial)
      state += " · Unofficial";

    auto* detail = new brls::Label();
    detail->setText(state);
    detail->setFontSize(15);
    detail->setTextColor(ThemeColour(achievement.unlocked ? "brls/list/listItem_value_color" :
                                                            "brls/text_disabled"));
    detail->setMarginTop(4);
    text->addView(detail);

    addView(row);
  }

  brls::Label* m_summary;
  std::vector<brls::Image*> m_badges;

  std::stop_source m_stop_source;
  std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};
}  // namespace

brls::View* CreateAchievementAccountView()
{
  return new AccountView();
}

brls::View* CreateGameAchievementsView(const std::string& path)
{
  return new GameAchievementsView(path);
}

void StopAchievementFetches()
{
  for (FetchWorker& worker : s_workers)
    worker.thread.request_stop();
  s_workers.clear();
}
}  // namespace Shell
