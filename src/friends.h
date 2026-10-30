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
    int32_t last_count, last_total, last_valid, last_named, last_playing, last_online;
    uint64_t started_ms, announce_ms, next_request_ms, self;
    int32_t callback_ids[48];
    unsigned callback_count;
    int32_t announce_state;
    bool announce_pending; /* announce_state still to be sent (it may be 0, Offline) */
    void *shared;          /* IClientSharedConnection, or NULL */
    uint32_t connection;   /* its allocated connection */
    int32_t persona_flags; /* PersonaStateChange_t flags seen, OR-ed */
    int32_t persona_updates;
    /* Lobby invites received for this app (LobbyInvite_t), newest kept. */
    struct { uint64_t from, lobby; } invites[16];
    unsigned invite_count;
};

void sh_friends_init(struct sh_friends *f, HMODULE module, void *engine, int32_t user, int32_t pipe,
                     uint64_t self, uint32_t appid, const struct dock_client_layout *layout, const struct sh_observer *o);
/* Rate-limited; call from the launch loop. Invites need the game running. */
void sh_friends_poll(struct sh_friends *f, bool game_running);
/* Records each distinct callback ID seen on the pipe (numbers only), to
 * show which notifications reach this session. */
void sh_friends_callback(struct sh_friends *f, int32_t id, const void *data, int32_t size);
/* Removes the feed so the app never reads a stale list. */
void sh_friends_end(struct sh_friends *f);
#endif
