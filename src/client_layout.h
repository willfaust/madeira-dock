/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 125hz
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md */
#ifndef MADEIRA_DOCK_CLIENT_LAYOUT_H
#define MADEIRA_DOCK_CLIENT_LAYOUT_H
#include <stdint.h>
#include <stdbool.h>
struct dock_client_layout {
    const char *sha256;
    int revision;
    uintptr_t engine_user, engine_result, engine_manager;
    uintptr_t logon, logged_on, connected, cached, select_account, token;
    uintptr_t running, subscribed, subscriptions, launch, install_dir;
    /* ml1990: IClientUserMap slot 71, RequestCustomBinaries (see CLIENT_LAYOUTS.md). */
    uintptr_t ceg_request;
    /* Offline logon: IClientUserMap slots 5 GetLogonState, 214 CanLogonOffline,
     * 215 LogOnOffline (see CLIENT_LAYOUTS.md). Valve's client decides whether the
     * account may log on offline, from the offline logon ticket it fetched itself
     * during an earlier online logon. */
    uintptr_t logon_state, can_offline, logon_offline;
    /* Friends (see CLIENT_LAYOUTS.md): CSteamClient slot 13 GetIClientFriends, then
     * IClientFriendsMap slots 7 GetFriendCount, 10 GetFriendByIndex, 13
     * GetFriendPersonaState, 14 GetFriendPersonaName, 23 GetFriendGamePlayed,
     * 93 GetFriendRichPresence, 215 InviteUserToGame, 194 NotifyLobbyJoinRequested,
     * 195 NotifyRichPresenceJoinRequested, 4 GetPersonaState, 5 SetPersonaState. Zero where a build was not inspected:
     * the friends feature is then unavailable and the launch is unaffected. */
    uintptr_t engine_friends, friend_count, friend_by_index, friend_state, friend_name,
        friend_game, friend_presence, friend_invite, friend_join_lobby, friend_join_presence,
        persona_state, set_persona_state;
};
const struct dock_client_layout *dock_client_layout(const char *sha256);
bool dock_method_is(uintptr_t module, void *object, unsigned slot, uintptr_t rva);
#endif
