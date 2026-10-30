/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 125hz
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md
 * Friends through Valve's client: list, invite, and join through the client's
 * own accepted-invite path (NotifyLobbyJoinRequested /
 * NotifyRichPresenceJoinRequested), which posts the running game's own join
 * callbacks. Slots and ABI: docs/CLIENT_LAYOUTS.md, "Friends research".
 */
#include "friends.h"
#include <stdio.h>
#include <string.h>

#define SH_FRIENDS_FEED_MS 3000
#define SH_FRIEND_FLAG_IMMEDIATE 4

#ifdef _WIN64
static const wchar_t feed_path[] = L"C:\\madeira-friends.bin";
static const wchar_t feed_temp[] = L"C:\\madeira-friends.tmp";
static const wchar_t command_path[] = L"C:\\madeira-friends-cmd.bin";
/* FriendGameInfo_t, 24 bytes (GetFriendGamePlayed reads exactly 0x18). */
struct game_info {
    uint64_t game;
    uint32_t ip;
    uint16_t port, query_port;
    uint64_t lobby;
};
_Static_assert(sizeof(struct game_info) == 24, "FriendGameInfo_t");

typedef void *(__thiscall *get_friends_fn)(void *, int32_t, int32_t);
typedef int32_t (__thiscall *count_fn)(void *, int32_t);
typedef int32_t (__thiscall *own_state_fn)(void *);
typedef void *(__thiscall *by_index_fn)(void *, uint64_t *, int32_t, int32_t);
typedef int32_t (__thiscall *state_fn)(void *, uint64_t);
typedef const char *(__thiscall *name_fn)(void *, uint64_t);
typedef bool (__thiscall *game_fn)(void *, uint64_t, struct game_info *);
typedef const char *(__thiscall *presence_fn)(void *, uint32_t, uint64_t, const char *);
typedef bool (__thiscall *invite_fn)(void *, uint32_t, uint64_t, const char *);
typedef bool (__thiscall *join_lobby_fn)(void *, uint32_t, uint64_t, uint64_t);
typedef bool (__thiscall *join_presence_fn)(void *, uint32_t, uint64_t, const char *);
typedef void *(__thiscall *get_shared_fn)(void *, int32_t, int32_t);
typedef uint32_t (__thiscall *allocate_fn)(void *);
typedef bool (__thiscall *send_fn)(void *, uint32_t, const void *, uint32_t);

#define SLOT(f, n) ((*(void ***)(f)->friends)[n])
#define SHARED(f, n) ((*(void ***)(f)->shared)[n])

static bool disabled(void)
{
    wchar_t value[4] = {0};
    if (GetEnvironmentVariableW(L"MADEIRA_DOCK_FRIENDS", value, 4) == 1 && value[0] == L'0') return true;
    /* The app's Offline choice (MADEIRA_DOCK_PERSONA_STATE=0) turns friends off
     * entirely: no sign-in to Friends, no subscription, no list, no commands. */
    return GetEnvironmentVariableW(L"MADEIRA_DOCK_PERSONA_STATE", value, 4) == 1 && value[0] == L'0';
}

void sh_friends_init(struct sh_friends *f, HMODULE module, void *engine, int32_t user, int32_t pipe,
                     uint64_t self, uint32_t appid, const struct dock_client_layout *layout, const struct sh_observer *o)
{
    memset(f, 0, sizeof *f);
    f->o = o;
    f->appid = appid;
    f->self = self;
    f->last_count = f->last_total = f->last_valid = f->last_named = f->last_playing = f->last_online = -1;
    f->started_ms = o->now_ms();
    DeleteFileW(feed_path);
    DeleteFileW(command_path);
    uintptr_t base = (uintptr_t)module;
    if (disabled() || !layout || !dock_method_is(base, engine, 13, layout->engine_friends)) {
        o->event("friends-available", 0);
        return;
    }
    void *friends = ((get_friends_fn)(*(void ***)engine)[13])(engine, user, pipe);
    const struct { unsigned slot; uintptr_t rva; } methods[] = {
        {7, layout->friend_count}, {10, layout->friend_by_index}, {13, layout->friend_state},
        {14, layout->friend_name}, {23, layout->friend_game}, {93, layout->friend_presence},
        {215, layout->friend_invite}, {194, layout->friend_join_lobby}, {195, layout->friend_join_presence},
        {4, layout->persona_state}, {5, layout->set_persona_state}, {32, layout->request_user_info}};
    for (unsigned i = 0; i < sizeof methods / sizeof methods[0]; ++i)
        if (!dock_method_is(base, friends, methods[i].slot, methods[i].rva)) {
            o->event("friends-available", 0);
            return;
        }
    f->friends = friends;
    f->layout = layout;
    o->event("friends-available", 1);
    /* Desktop Steam's Friends window, not the client, signs in to Friends:
     * with a web-based UI the client leaves the status change to the window
     * (SetPersonaStateInternal_t). The window sends ClientChangeStatus and
     * ClientRequestFriendData itself through the client's shared connection,
     * so Dock sends the same two messages that way (sh_friends_poll).
     * MADEIRA_DOCK_FRIENDS_ONLINE=0 sends neither. */
    int32_t own = ((own_state_fn)SLOT(f, 4))(friends);
    o->event("friends-own-state", own);
    wchar_t online[4] = {0};
    bool keep = GetEnvironmentVariableW(L"MADEIRA_DOCK_FRIENDS_ONLINE", online, 4) == 1 && online[0] == L'0';
    if (keep || !dock_method_is(base, engine, 69, layout->engine_shared)) {
        o->event("friends-signin-available", 0);
        return;
    }
    void *shared = ((get_shared_fn)(*(void ***)engine)[69])(engine, user, pipe);
    if (!shared || !dock_method_is(base, shared, 0, layout->shared_allocate) ||
        !dock_method_is(base, shared, 2, layout->shared_send)) {
        o->event("friends-signin-available", 0);
        return;
    }
    f->shared = shared;
    f->connection = ((allocate_fn)SHARED(f, 0))(shared);
    o->event("friends-signin-available", f->connection ? 1 : 2);
    if (!f->connection) { f->shared = NULL; return; }
    /* The app's choice (MADEIRA_DOCK_PERSONA_STATE: 0 Offline, 1 Online, 3 Away,
     * 7 Invisible); without one, the stored state (Online on a fresh session),
     * where Offline becomes Online. */
    wchar_t chosen[4] = {0};
    DWORD n = GetEnvironmentVariableW(L"MADEIRA_DOCK_PERSONA_STATE", chosen, 4);
    if (n == 1 && (chosen[0] == L'0' || chosen[0] == L'1' || chosen[0] == L'3' || chosen[0] == L'7')) {
        f->announce_state = (int32_t)(chosen[0] - L'0');
    } else {
        f->announce_state = own > 0 ? own : 1;
    }
    f->announce_pending = true;
    o->event("friends-persona-chosen", f->announce_state);
    f->announce_ms = o->now_ms() + 2000;
}

static uint32_t game_app(const struct game_info *game)
{
    /* CGameID: type in the top byte (0 = app), App ID in the low 24 bits. */
    return (game->game >> 56) == 0 ? (uint32_t)(game->game & 0xffffff) : 0;
}

/* Copies the friend's rich presence "connect" into out when it is valid. */
static bool friend_connect(struct sh_friends *f, uint64_t id, char *out)
{
    const char *value = ((presence_fn)SLOT(f, 93))(f->friends, f->appid, id, "connect");
    if (!sh_friend_connect_valid(value)) return false;
    strcpy(out, value);
    return true;
}

/* The lobby this friend invited this player to, or 0. */
static uint64_t invited_lobby(const struct sh_friends *f, uint64_t from)
{
    for (unsigned i = 0; i < f->invite_count; ++i) if (f->invites[i].from == from) return f->invites[i].lobby;
    return 0;
}

static void forget_invite(struct sh_friends *f, uint64_t from)
{
    for (unsigned i = 0; i < f->invite_count; ++i)
        if (f->invites[i].from == from) {
            f->invites[i] = f->invites[--f->invite_count];
            return;
        }
}

static bool friend_known(struct sh_friends *f, uint64_t id)
{
    int32_t count = ((count_fn)SLOT(f, 7))(f->friends, SH_FRIEND_FLAG_IMMEDIATE);
    for (int32_t i = 0; i < count && i < 2000; ++i) {
        uint64_t other = 0;
        ((by_index_fn)SLOT(f, 10))(f->friends, &other, i, SH_FRIEND_FLAG_IMMEDIATE);
        if (other == id) return true;
    }
    return false;
}

static void write_feed(struct sh_friends *f)
{
    static struct sh_friend list[SH_FRIENDS_MAX];
    static uint8_t feed[SH_FRIENDS_FEED_CAP];
    int32_t total = ((count_fn)SLOT(f, 7))(f->friends, SH_FRIEND_FLAG_IMMEDIATE);
    uint32_t count = 0;
    int32_t valid = 0, named = 0, playing = 0, online = 0;
    /* Like desktop Steam, Invite is offered only when this player has something
     * to join: a lobby of this app, or a rich presence "connect" the game set. */
    struct game_info mine = {0};
    char mine_connect[SH_FRIEND_CONNECT_CAP];
    bool in_lobby = ((game_fn)SLOT(f, 23))(f->friends, f->self, &mine) && mine.lobby && game_app(&mine) == f->appid;
    in_lobby = in_lobby || friend_connect(f, f->self, mine_connect);
    for (int32_t i = 0; i < total && i < 2000 && count < SH_FRIENDS_MAX; ++i) {
        uint64_t id = 0;
        ((by_index_fn)SLOT(f, 10))(f->friends, &id, i, SH_FRIEND_FLAG_IMMEDIATE);
        if (sh_friend_steamid_valid(id)) {
            ++valid;
            const char *name = ((name_fn)SLOT(f, 14))(f->friends, id);
            struct game_info probe = {0};
            if (name && name[0]) ++named;
            if (((game_fn)SLOT(f, 23))(f->friends, id, &probe) && probe.game) ++playing;
        }
        int32_t state = sh_friend_steamid_valid(id) ? ((state_fn)SLOT(f, 13))(f->friends, id) : 0;
        /* All friends are listed, online or not. */
        if (!sh_friend_steamid_valid(id)) continue;
        if (state < 0) state = 0;
        if (state > 0) ++online;
        struct sh_friend *entry = &list[count++];
        memset(entry, 0, sizeof *entry);
        entry->steamid = id;
        entry->state = (uint32_t)state;
        sh_friend_name_copy(entry->name, sizeof entry->name, ((name_fn)SLOT(f, 14))(f->friends, id));
        if (invited_lobby(f, id)) entry->flags |= SH_FRIEND_INVITED;
        if (in_lobby) entry->flags |= SH_FRIEND_CAN_INVITE;
        /* A friend who appears offline (invisible included) shows no game,
         * as in desktop Steam, though the client may still know one. */
        struct game_info game = {0};
        if (state > 0 && ((game_fn)SLOT(f, 23))(f->friends, id, &game) && game.game) {
            entry->flags |= SH_FRIEND_IN_GAME;
            entry->appid = game_app(&game);
            if (entry->appid && entry->appid == f->appid) {
                char connect[SH_FRIEND_CONNECT_CAP];
                entry->flags |= SH_FRIEND_SAME_APP;
                if (game.lobby || friend_connect(f, id, connect)) entry->flags |= SH_FRIEND_JOINABLE;
            }
        }
    }
    /* As the Friends window does: ask for persona data only for friends whose
     * name has not loaded, at most once a minute. Asking for loaded friends
     * resets their live status to offline; status comes from the
     * Chat.RequestFriendPersonaStates subscription (sh_friends_poll). */
    uint64_t now = f->o->now_ms();
    if (f->shared && !f->announce_pending && valid && now >= f->next_request_ms) {
        static uint64_t ids[SH_FRIENDS_MAX];
        static uint8_t message[8 + 14 + SH_FRIENDS_MAX * 9];
        uint32_t n = 0;
        for (uint32_t i = 0; i < count; ++i) if (!list[i].name[0]) ids[n++] = list[i].steamid;
        f->next_request_ms = now + 60000;
        if (n) {
            size_t size = sh_cm_request_friend_data(message, sizeof message, SH_CM_FRIEND_DATA_FLAGS, ids, n);
            bool sent = size && ((send_fn)SHARED(f, 2))(f->shared, f->connection, message, (uint32_t)size);
            f->o->event("friends-requested", sent ? (int32_t)n : -1);
        }
    }
    /* Counts only, to tell an empty list from a failing step. */
    if (total != f->last_total) f->o->event("friends-total", total);
    if (valid != f->last_valid) f->o->event("friends-valid-ids", valid);
    if (named != f->last_named || playing != f->last_playing || online != f->last_online ||
        (int32_t)count != f->last_count) {
        f->o->event("friends-online", online);
        f->o->event("friends-named", named);
        f->o->event("friends-playing", playing);
        f->o->event("friends-seconds", (int32_t)((f->o->now_ms() - f->started_ms) / 1000));
    }
    f->last_total = total;
    f->last_valid = valid;
    f->last_named = named;
    f->last_playing = playing;
    f->last_online = online;
    size_t size = sh_friends_feed_encode(feed, sizeof feed, list, count);
    HANDLE file = size ? CreateFileW(feed_temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL) : INVALID_HANDLE_VALUE;
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    bool ok = WriteFile(file, feed, (DWORD)size, &written, NULL) && written == size;
    CloseHandle(file);
    if (!ok || !MoveFileExW(feed_temp, feed_path, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(feed_temp);
        return;
    }
    if ((int32_t)count != f->last_count) f->o->event("friends-count", (int32_t)count);
    f->last_count = (int32_t)count;
}

static int32_t run_command(struct sh_friends *f, const struct sh_friend_command *c, bool game_running)
{
    if (c->op == SH_FRIEND_SET_STATE) {
        uint8_t message[64];
        size_t size = f->shared ? sh_cm_change_status(message, sizeof message, (uint32_t)c->steamid) : 0;
        bool sent = size && ((send_fn)SHARED(f, 2))(f->shared, f->connection, message, (uint32_t)size);
        f->o->event("friends-state-set", sent ? (int32_t)c->steamid : -1);
        return sent ? SH_FRIEND_DONE : SH_FRIEND_CALL_FAILED;
    }
    if (!friend_known(f, c->steamid)) return SH_FRIEND_UNKNOWN;
    if (c->op == SH_FRIEND_INVITE) {
        if (!game_running) return SH_FRIEND_NOT_JOINABLE;
        /* The invite carries this player's own join details: in a lobby of this
         * app, the friends menu's "Invite to lobby" (a lobby invite, which games
         * accept through their lobby join callback); else the game's rich
         * presence "connect"; else its lobby as +connect_lobby. With none of
         * these there is nothing to join and no invite is sent. */
        char connect[SH_FRIEND_CONNECT_CAP] = "";
        int32_t kind = 0;
        struct game_info own = {0};
        /* What the client knows of this player's own game: 0 nothing, 1 no
         * game, 2 another app, 3 this app without a lobby, 4 with a lobby. */
        bool have = ((game_fn)SLOT(f, 23))(f->friends, f->self, &own);
        f->o->event("friends-own-game", !have ? 0 : !own.game ? 1 : game_app(&own) != f->appid ? 2 : own.lobby ? 4 : 3);
        bool lobby = have && own.lobby && game_app(&own) == f->appid;
        if (lobby && f->shared) {
            uint8_t message[40];
            size_t size = sh_cm_invite_to_lobby(message, sizeof message, f->appid, own.lobby, c->steamid);
            f->o->event("friends-invite-connect", 3);
            return size && ((send_fn)SHARED(f, 2))(f->shared, f->connection, message, (uint32_t)size)
                ? SH_FRIEND_DONE : SH_FRIEND_CALL_FAILED;
        }
        if (friend_connect(f, f->self, connect)) kind = 1;
        else if (lobby) {
            snprintf(connect, sizeof connect, "+connect_lobby %llu", (unsigned long long)own.lobby);
            kind = 2;
        }
        f->o->event("friends-invite-connect", kind);
        if (!kind) return SH_FRIEND_NOT_JOINABLE;
        return ((invite_fn)SLOT(f, 215))(f->friends, f->appid, c->steamid, connect) ? SH_FRIEND_DONE : SH_FRIEND_CALL_FAILED;
    }
    /* An invite from this friend names the lobby to join. */
    uint64_t invited = invited_lobby(f, c->steamid);
    if (invited) {
        forget_invite(f, c->steamid);
        return ((join_lobby_fn)SLOT(f, 194))(f->friends, f->appid, invited, c->steamid)
            ? SH_FRIEND_DONE : SH_FRIEND_CALL_FAILED;
    }
    struct game_info game = {0};
    if (((state_fn)SLOT(f, 13))(f->friends, c->steamid) <= 0 ||
        !((game_fn)SLOT(f, 23))(f->friends, c->steamid, &game) || game_app(&game) != f->appid)
        return SH_FRIEND_NOT_JOINABLE;
    if (game.lobby)
        return ((join_lobby_fn)SLOT(f, 194))(f->friends, f->appid, game.lobby, c->steamid)
            ? SH_FRIEND_DONE : SH_FRIEND_CALL_FAILED;
    char connect[SH_FRIEND_CONNECT_CAP];
    if (!friend_connect(f, c->steamid, connect)) return SH_FRIEND_NOT_JOINABLE;
    return ((join_presence_fn)SLOT(f, 195))(f->friends, f->appid, c->steamid, connect)
        ? SH_FRIEND_DONE : SH_FRIEND_CALL_FAILED;
}

static void read_command(struct sh_friends *f, bool game_running)
{
    HANDLE file = CreateFileW(command_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    uint8_t data[SH_FRIENDS_COMMAND_SIZE + 1];
    DWORD size = 0;
    bool read = ReadFile(file, data, sizeof data, &size, NULL);
    CloseHandle(file);
    DeleteFileW(command_path);
    struct sh_friend_command command;
    int32_t result = SH_FRIEND_BAD_COMMAND;
    if (read && sh_friend_command_parse(data, size, &command)) {
        if (command.sequence == f->last_sequence) return;   /* already answered */
        f->last_sequence = command.sequence;
        f->o->event("friends-cmd-seq", (int32_t)command.sequence);
        result = f->friends ? run_command(f, &command, game_running) : SH_FRIEND_UNAVAILABLE;
    }
    f->o->event("friends-cmd-result", result);
}

void sh_friends_poll(struct sh_friends *f, bool game_running)
{
    uint64_t now = f->o->now_ms();
    if (now < f->next_command_ms) return;
    f->next_command_ms = now + SH_FRIENDS_FEED_MS / 6;
    read_command(f, game_running);
    if (f->shared && f->announce_pending && now >= f->announce_ms) {
        uint8_t message[64];
        size_t size = sh_cm_change_status(message, sizeof message, (uint32_t)f->announce_state);
        bool sent = size && ((send_fn)SHARED(f, 2))(f->shared, f->connection, message, (uint32_t)size);
        f->o->event("friends-signin", sent ? f->announce_state : -1);
        /* Then, as the window does once the friends list is in, subscribe to
         * friends' status; without it Steam sends their names only. */
        size = sh_cm_service_call(message, sizeof message, "Chat.RequestFriendPersonaStates#1");
        sent = size && ((send_fn)SHARED(f, 2))(f->shared, f->connection, message, (uint32_t)size);
        f->o->event("friends-subscribed", sent);
        f->announce_pending = false;
    }
    if (!f->friends || now < f->next_feed_ms) return;
    f->next_feed_ms = now + SH_FRIENDS_FEED_MS;
    write_feed(f);
}

void sh_friends_callback(struct sh_friends *f, int32_t id, const void *data, int32_t size)
{
    if (!f->friends) return;
    /* PersonaStateChange_t {uint64 SteamID; int32 flags}: which kinds of
     * change (1 name, 2 status, 4 came online, 8 went offline, 16 game, ...)
     * reach this session. Counts and flag bits only. */
    /* LobbyInvite_t {inviter, lobby, CGameID}: a friend invited this player
     * to a lobby. Kept for this app only (the newest per friend); Join on
     * that friend joins it. */
    if (id == 503 && size >= 24) {
        uint64_t from, lobby, game;
        memcpy(&from, data, 8);
        memcpy(&lobby, (const uint8_t *)data + 8, 8);
        memcpy(&game, (const uint8_t *)data + 16, 8);
        struct game_info info = {.game = game};
        bool ours = game_app(&info) == f->appid && lobby && sh_friend_steamid_valid(from);
        f->o->event("friends-invite-received", ours ? 1 : 0);
        if (ours) {
            forget_invite(f, from);
            if (f->invite_count == sizeof f->invites / sizeof f->invites[0]) {
                memmove(f->invites, f->invites + 1, sizeof f->invites - sizeof f->invites[0]);
                --f->invite_count;
            }
            f->invites[f->invite_count].from = from;
            f->invites[f->invite_count].lobby = lobby;
            ++f->invite_count;
            f->next_feed_ms = 0;   /* show it at the next poll */
        }
    }
    if (id == 304 && size >= 12) {
        uint64_t who;
        int32_t flags;
        memcpy(&who, data, sizeof who);
        memcpy(&flags, (const uint8_t *)data + 8, sizeof flags);
        if (who != f->self) {
            ++f->persona_updates;
            if ((f->persona_flags | flags) != f->persona_flags) {
                f->persona_flags |= flags;
                f->o->event("friends-persona-flags", f->persona_flags);
            }
            if ((f->persona_updates & 63) == 1) f->o->event("friends-persona-updates", f->persona_updates);
        }
    }
    for (unsigned i = 0; i < f->callback_count; ++i) if (f->callback_ids[i] == id) return;
    if (f->callback_count >= sizeof f->callback_ids / sizeof f->callback_ids[0]) return;
    f->callback_ids[f->callback_count++] = id;
    f->o->event("friends-callback-id", id);
}

void sh_friends_end(struct sh_friends *f)
{
    (void)f;
    DeleteFileW(feed_path);
    DeleteFileW(feed_temp);
    DeleteFileW(command_path);
}
#else
void sh_friends_init(struct sh_friends *f, HMODULE module, void *engine, int32_t user, int32_t pipe,
                     uint64_t self, uint32_t appid, const struct dock_client_layout *layout, const struct sh_observer *o)
{
    (void)module; (void)engine; (void)user; (void)pipe; (void)self; (void)appid; (void)layout;
    memset(f, 0, sizeof *f);
    f->o = o;
    o->event("friends-available", 0);
}
void sh_friends_poll(struct sh_friends *f, bool game_running) { (void)f; (void)game_running; }
void sh_friends_callback(struct sh_friends *f, int32_t id, const void *data, int32_t size) { (void)f; (void)id; (void)data; (void)size; }
void sh_friends_end(struct sh_friends *f) { (void)f; }
#endif
