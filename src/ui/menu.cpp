/*
** Taiga
** Copyright (C) 2010-2018, Eren Okka
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <windows/win/string.h>

#include "base/format.h"
#include "base/string.h"
#include "base/xml.h"
#include "library/anime_db.h"
#include "library/anime_episode.h"
#include "library/anime_util.h"
#include "library/discover.h"
#include "sync/anilist_util.h"
#include "sync/kitsu_util.h"
#include "sync/myanimelist_util.h"
#include "sync/sync.h"
#include "taiga/settings.h"
#include "track/feed.h"
#include "ui/menu.h"

#include "dlg/dlg_anime_list.h"
#include "dlg/dlg_main.h"
#include "dlg/dlg_season.h"

namespace ui {

MenuList Menus;

void MenuList::Load() {
  menu_list_.menus.clear();

  std::wstring menu_resource;
  win::ReadStringFromResource(L"IDR_MENU", L"DATA", menu_resource);

  xml_document document;
  xml_parse_result parse_result = document.load_string(menu_resource.data());

  xml_node menus = document.child(L"menus");

  foreach_xmlnode_(menu, menus, L"menu") {
    const std::wstring name = menu.attribute(L"name").value();
    menu_list_.Create(name, menu.attribute(L"type").value());
    foreach_xmlnode_(item, menu, L"item") {
      menu_list_.menus[name].CreateItem(
          item.attribute(L"action").value(),
          item.attribute(L"name").value(),
          item.attribute(L"sub").value(),
          item.attribute(L"checked").as_bool(),
          item.attribute(L"default").as_bool(),
          !item.attribute(L"disabled").as_bool(),
          item.attribute(L"column").as_bool(),
          item.attribute(L"radio").as_bool());
    }
  }
}

std::wstring MenuList::Show(HWND hwnd, int x, int y, LPCWSTR lpName) {
  return menu_list_.Show(hwnd, x, y, lpName);
}

void MenuList::UpdateAnime(const anime::Item* anime_item) {
  if (!anime_item)
    return;
  if (!anime_item->IsInList())
    return;

  // Edit
  auto menu = menu_list_.FindMenu(L"Edit");
  if (menu) {
    for (auto& item : menu->items) {
      if (item.action == L"EditTags" ||
          item.action == L"EditNotes") {
        switch (taiga::GetCurrentServiceId()) {
          case sync::kMyAnimeList:
            item.name = L"Set &tags...";
            item.action = L"EditTags";
            break;
          case sync::kKitsu:
          case sync::kAniList:
            item.name = L"Set &notes...";
            item.action = L"EditNotes";
            break;
        }
      }
    }
  }

  // Edit > Score
  UpdateScore(anime_item);

  // Edit > Status
  menu = menu_list_.FindMenu(L"EditStatus");
  if (menu) {
    for (auto& item : menu->items) {
      item.checked = false;
      item.def = false;
    }
    int item_index = anime_item->GetMyStatus();
    if (item_index - 1 < static_cast<int>(menu->items.size())) {
      menu->items[item_index - 1].checked = true;
      menu->items[item_index - 1].def = true;
    }
  }

  // Play
  menu = menu_list_.FindMenu(L"RightClick");
  if (menu) {
    for (int i = static_cast<int>(menu->items.size()) - 1; i > 0; i--) {
      if (menu->items[i].type == win::kMenuItemSeparator) {
        // Clear items
        menu->items.resize(i + 1);
        // Play episode
        if (anime_item->GetEpisodeCount() != 1) {
          menu->CreateItem(L"", L"Play episode", L"PlayEpisode");
        }
        // Play last episode
        if (anime_item->GetMyLastWatchedEpisode() > 0) {
          menu->CreateItem(L"PlayLast()",
                           L"Play last episode (#{})"_format(
                             anime_item->GetMyLastWatchedEpisode()));
        }
        // Play next episode
        if (!anime::IsValidEpisodeCount(anime_item->GetEpisodeCount()) ||
            anime_item->GetMyLastWatchedEpisode() < anime_item->GetEpisodeCount()) {
          menu->CreateItem(L"PlayNext()",
                           L"Play next episode (#{})\tCtrl+N"_format(
                             anime_item->GetMyLastWatchedEpisode() + 1));
        }
        // Play random episode
        if (anime_item->GetEpisodeCount() != 1) {
          menu->CreateItem(L"PlayRandom()", L"Play random episode\tCtrl+R");
        }
        // Start new rewatch
        if (anime::IsValidEpisodeCount(anime_item->GetEpisodeCount()) &&
            anime_item->GetMyLastWatchedEpisode() == anime_item->GetEpisodeCount() &&
            anime_item->GetMyRewatching() == FALSE &&
            anime_item->GetAiringStatus() == anime::kFinishedAiring) {
          menu->CreateItem(L"StartNewRewatch()", L"Start new rewatch");
        }
        break;
      }
    }
  }

  // Play > Episode
  menu = menu_list_.FindMenu(L"PlayEpisode");
  if (menu) {
    // Clear menu
    menu->items.clear();

    // Add episode numbers
    int count_max = 0;
    int count_column = 0;
    if (anime_item->GetEpisodeCount() > 0) {
      count_max = anime_item->GetEpisodeCount();
    } else {
      count_max = anime_item->GetEpisodeCount();
      if (count_max == 0) {
        count_max = anime_item->GetMyLastWatchedEpisode() + 1;
      }
    }
    for (int i = 1; i <= count_max; i++) {
      count_column = count_max % 12 == 0 ? 12 : 13;
      if (count_max > 52)
        count_column *= 2;
      menu->CreateItem(L"PlayEpisode({})"_format(i),
                       L"#{}"_format(i),
                       L"",
                       i <= anime_item->GetMyLastWatchedEpisode(),
                       false,
                       true,
                       (i > 1) && (i % count_column == 1),
                       false);
    }
  }
}

void MenuList::UpdateAnimeListHeaders() {
  auto menu = menu_list_.FindMenu(L"AnimeListHeaders");
  if (menu) {
    for (auto& item : menu->items) {
      auto column_type = AnimeListDialog::ListView::TranslateColumnName(item.action);
      if (column_type != kColumnUnknown)
        item.checked = ui::DlgAnimeList.listview.columns[column_type].visible;
    }
  }
}

void MenuList::UpdateAnnounce() {
  // List > Announce current episode
  auto menu = menu_list_.FindMenu(L"List");
  if (menu) {
    for (auto& item : menu->items) {
      if (item.submenu == L"Announce") {
        item.enabled = CurrentEpisode.anime_id > 0;
        break;
      }
    }
  }
}

void MenuList::UpdateExport() {
  auto menu = menu_list_.FindMenu(L"Export");
  if (menu) {
    for (auto& item : menu->items) {
      if (item.action == L"ExportAsMalXml") {
        item.enabled = taiga::GetCurrentServiceId() == sync::kMyAnimeList;
        break;
      }
    }
  }
}

void MenuList::UpdateExternalLinks() {
  auto menu = menu_list_.FindMenu(L"ExternalLinks");
  if (menu) {
    // Clear menu
    menu->items.clear();

    std::vector<std::wstring> lines;
    Split(Settings[taiga::kApp_Interface_ExternalLinks], L"\r\n", lines);
    for (const auto& line : lines) {
      if (IsEqual(line, L"-")) {
        // Add separator
        menu->CreateItem();
      } else {
        std::vector<std::wstring> content;
        Split(line, L"|", content);
        if (content.size() > 1) {
          menu->CreateItem(L"URL(" + content.at(1) + L")", content.at(0));
        }
      }
    }
  }
}

void MenuList::UpdateFolders() {
  auto menu = menu_list_.FindMenu(L"Folders");
  if (menu) {
    // Clear menu
    menu->items.clear();

    if (!Settings.library_folders.empty()) {
      // Add folders
      for (size_t i = 0; i < Settings.library_folders.size(); ++i) {
        const auto& library_folder = Settings.library_folders.at(i);
        auto name = library_folder;
        if (i <= 9)
          name += L"\tAlt+" + ToWstr(i + 1);
        menu->CreateItem(L"Execute(" + library_folder + L")", name);
      }
      // Add separator
      menu->CreateItem();
    }

    // Add default item
    menu->CreateItem(L"AddFolder()", L"Add new folder...");
  }
}

void MenuList::UpdateHistoryList(bool enabled) {
  auto menu = menu_list_.FindMenu(L"HistoryList");
  if (menu) {
    // Add to list
    for (auto& item : menu->items) {
      if (item.action == L"Info()" ||
          item.action == L"Delete()") {
        item.enabled = enabled;
      } else if (item.action == L"ClearHistory()") {
        item.enabled = !History.items.empty();
      } else if (item.action == L"ClearQueue()") {
        item.enabled = History.queue.GetItemCount() && !History.queue.updating;
      }
    }
  }
}

void MenuList::UpdateScore(const anime::Item* anime_item) {
  auto menu = menu_list_.FindMenu(L"EditScore");
  if (menu) {
    menu->items.clear();

    std::vector<sync::Rating> ratings;
    std::wstring current_rating;

    switch (taiga::GetCurrentServiceId()) {
      case sync::kMyAnimeList:
        ratings = sync::myanimelist::GetMyRatings();
        current_rating = sync::myanimelist::TranslateMyRating(
            anime_item->GetMyScore(), true);
        break;
      case sync::kKitsu: {
        const auto rating_system = sync::kitsu::GetRatingSystem();
        ratings = sync::kitsu::GetMyRatings(rating_system);
        current_rating = sync::kitsu::TranslateMyRating(
            anime_item->GetMyScore(), rating_system);
        break;
      }
      case sync::kAniList: {
        const auto rating_system = sync::anilist::GetRatingSystem();
        ratings = sync::anilist::GetMyRatings(rating_system);
        current_rating = sync::anilist::TranslateMyRating(
            anime_item->GetMyScore(), rating_system);
        break;
      }
    }

    for (const auto& rating : ratings) {
      const bool current = rating.text == current_rating;
      menu->CreateItem(L"EditScore({})"_format(rating.value), rating.text,
                       L"", current, current, true, false, true);
    }
  }
}

void MenuList::UpdateSearchList(bool enabled) {
  auto menu = menu_list_.FindMenu(L"SearchList");
  if (menu) {
    // Add to list
    for (auto& item : menu->items) {
      if (item.submenu == L"AddToList") {
        item.enabled = enabled;
        break;
      }
    }
  }
}

void MenuList::UpdateSeasonList(bool enabled) {
  auto menu = menu_list_.FindMenu(L"SeasonList");
  if (menu) {
    // Add to list
    for (auto& item : menu->items) {
      if (item.submenu == L"AddToList") {
        item.enabled = enabled;
        break;
      }
    }
  }
}

void MenuList::UpdateTorrentsList(const FeedItem& feed_item) {
  auto menu = menu_list_.FindMenu(L"TorrentListRightClick");
  if (menu) {
    bool anime_identified = anime::IsValidId(feed_item.episode_data.anime_id);
    for (auto& item : menu->items) {
      // Info
      if (item.action == L"Info") {
        item.enabled = anime_identified;
      // Torrent info
      } else if (item.action == L"TorrentInfo") {
        item.enabled = !feed_item.info_link.empty();
      // Quick filters
      } else if (item.submenu == L"QuickFilters") {
        item.enabled = anime_identified;
      }
    }
  }
}

void MenuList::UpdateSeason() {
  // Select season
  auto menu = menu_list_.FindMenu(L"SeasonSelect");
  if (menu) {
    menu->items.clear();

    auto season_min = SeasonDatabase.available_seasons.first;
    auto season_max = SeasonDatabase.available_seasons.second;
    switch (taiga::GetCurrentServiceId()) {
      case sync::kKitsu:
      case sync::kAniList: {
        const auto next_season = ++anime::Season(GetDate());
        season_max = std::max(season_max, next_season);
        season_min = anime::Season(anime::Season::Name::kWinter, 2010);
        break;
      }
    }

    auto create_item = [](win::Menu& menu, const anime::Season& season) {
      menu.CreateItem(L"Season_Load(" + season.GetString() + L")",
                      season.GetString(), L"",
                      season == SeasonDatabase.current_season,
                      false, true, false, true);
    };

    auto create_submenu = [this](const std::wstring& name) {
      auto submenu = menu_list_.FindMenu(name.c_str());
      if (!submenu) {
        menu_list_.Create(name.c_str(), L"");
        submenu = menu_list_.FindMenu(name.c_str());
      }
      return submenu;
    };

    auto create_available_seasons = [this, &create_item, &create_submenu](
        win::Menu& menu,
        const anime::Season& season_min,
        const anime::Season& season_max) {
      int current_year = 0;
      for (auto season = season_max; season >= season_min; --season) {
        std::wstring submenu_name = L"Season" + ToWstr(season.year);
        auto submenu = create_submenu(submenu_name);
        if (current_year == 0 || current_year != season.year) {
          menu.CreateItem(L"", ToWstr(season.year), submenu_name);
          submenu->items.clear();
          current_year = season.year;
        }
        create_item(*submenu, season);
      }
    };

    // Add latest seasons
    for (auto season = season_max; season >= season_min; --season) {
      create_item(*menu, season);
      if (menu->items.size() == 2)
        break;
    }

    if (!menu->items.empty())
      menu->CreateItem();  // separator

    // Add available seasons
    create_available_seasons(*menu, season_min, season_max);
    switch (taiga::GetCurrentServiceId()) {
      case sync::kKitsu:
      case sync::kAniList: {
        menu->CreateItem();  // separator
        for (int decade = 2000; decade >= 1960; decade -= 10) {
          std::wstring submenu_name = L"SeasonDecade" + ToWstr(decade);
          auto submenu = create_submenu(submenu_name);
          submenu->items.clear();
          menu->CreateItem(L"", ToWstr(decade) + L"s", submenu_name);
          create_available_seasons(*submenu,
              anime::Season(anime::Season::Name::kWinter, decade),
              anime::Season(anime::Season::Name::kFall, decade + 9));
        }
        break;
      }
    }
  }

  // Group by
  menu = menu_list_.FindMenu(L"SeasonGroup");
  if (menu) {
    for (auto& item : menu->items) {
      item.checked = false;
    }
    int item_index = Settings.GetInt(taiga::kApp_Seasons_GroupBy);
    if (item_index < static_cast<int>(menu->items.size())) {
      menu->items[item_index].checked = true;
    }
  }

  // Sort by
  menu = menu_list_.FindMenu(L"SeasonSort");
  if (menu) {
    for (auto& item : menu->items) {
      item.checked = false;
    }
    int item_index = Settings.GetInt(taiga::kApp_Seasons_SortBy);
    if (item_index < static_cast<int>(menu->items.size())) {
      menu->items[item_index].checked = true;
    }
  }

  // View as
  menu = menu_list_.FindMenu(L"SeasonView");
  if (menu) {
    for (auto& item : menu->items) {
      item.checked = false;
    }
    int item_index = Settings.GetInt(taiga::kApp_Seasons_ViewAs);
    if (item_index < static_cast<int>(menu->items.size())) {
      menu->items[item_index].checked = true;
    }
  }
}

void MenuList::UpdateServices(bool enabled) {
  auto menu = menu_list_.FindMenu(L"Services");
  if (menu) {
    for (auto& item : menu->items) {
      if (item.action == L"Synchronize()") {
        item.enabled = enabled;
        break;
      }
    }
  }
}

void MenuList::UpdateTools() {
  auto menu = menu_list_.FindMenu(L"Tools");
  if (menu) {
    for (auto& item : menu->items) {
      // Tools > Enable anime recognition
      if (item.action == L"ToggleRecognition()")
        item.checked = Settings.GetBool(taiga::kApp_Option_EnableRecognition);
      // Tools > Enable auto sharing
      if (item.action == L"ToggleSharing()")
        item.checked = Settings.GetBool(taiga::kApp_Option_EnableSharing);
      // Tools > Enable auto synchronization
      if (item.action == L"ToggleSynchronization()")
        item.checked = Settings.GetBool(taiga::kApp_Option_EnableSync);
    }
  }
}

void MenuList::UpdateTray() {
  auto menu = menu_list_.FindMenu(L"Tray");
  if (menu) {
    // Tray > Enable recognition
    for (auto& item : menu->items) {
      if (item.action == L"ToggleRecognition()") {
        item.checked = Settings.GetBool(taiga::kApp_Option_EnableRecognition);
        break;
      }
    }
  }
}

void MenuList::UpdateView() {
  auto menu = menu_list_.FindMenu(L"View");
  if (menu) {
    for (auto& item : menu->items) {
      item.checked = false;
    }
    int item_index = DlgMain.navigation.GetCurrentPage();
    for (auto& item : menu->items) {
      if (item.action == L"ViewContent(" + ToWstr(item_index) + L")") {
        item.checked = true;
        break;
      }
    }
    menu->items.back().checked = !Settings.GetBool(taiga::kApp_Option_HideSidebar);
  }
}

void MenuList::UpdateAll(const anime::Item* anime_item) {
  UpdateAnime(anime_item);
  UpdateAnnounce();
  UpdateExport();
  UpdateExternalLinks();
  UpdateFolders();
  UpdateTools();
  UpdateTray();
  UpdateView();
}

}  // namespace ui