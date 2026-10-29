/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 125hz
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md */
#ifndef MADEIRA_DOCK_FRIENDS_H
#define MADEIRA_DOCK_FRIENDS_H
#include "session.h"
#include "friends_format.h"

/* Friends feed and Join/Invite commands during a launched game. Optional:
 * when unavailable (unpinned client build, MADEIRA_DOCK_FRIENDS=0, a method
 * mismatch) it reports friends-available=0 and does nothing else; the launch
 * is never affected. SteamIDs and names go only to the feed file, never to
 * the report or a log. */
struct sh_friends {
    void *friends;
    const struct dock_client_layout *layout;
    const struct sh_observer *o;
    uint32_t appid, last_sequence;
    uint64_t next_command_ms, next_feed_ms;
    int32_t last_count;
};

void sh_friends_init(struct sh_friends *f, HMODULE module, void *engine, int32_t user, int32_t pipe,
                     uint32_t appid, const struct dock_client_layout *layout, const struct sh_observer *o);
/* Rate-limited; call from the launch loop. Invites need the game running. */
void sh_friends_poll(struct sh_friends *f, bool game_running);
/* Removes the feed so the app never reads a stale list. */
void sh_friends_end(struct sh_friends *f);
#endif
