/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 125hz
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md
 * Friends through Valve's client: list, invite, and join through the client's
 * own accepted-invite path (NotifyLobbyJoinRequested /
 * NotifyRichPresenceJoinRequested), which posts the running game's own join
 * callbacks. Slots and ABI: docs/CLIENT_LAYOUTS.md, "Friends research".
 */
#include "friends.h"
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
typedef void *(__thiscall *by_index_fn)(void *, uint64_t *, int32_t, int32_t);
typedef int32_t (__thiscall *state_fn)(void *, uint64_t);
typedef const char *(__thiscall *name_fn)(void *, uint64_t);
typedef bool (__thiscall *game_fn)(void *, uint64_t, struct game_info *);
typedef const char *(__thiscall *presence_fn)(void *, uint32_t, uint64_t, const char *);
typedef bool (__thiscall *invite_fn)(void *, uint32_t, uint64_t, const char *);
typedef bool (__thiscall *join_lobby_fn)(void *, uint32_t, uint64_t, uint64_t);
typedef bool (__thiscall *join_presence_fn)(void *, uint32_t, uint64_t, const char *);

#define SLOT(f, n) ((*(void ***)(f)->friends)[n])

static bool disabled(void)
{
    wchar_t value[4] = {0};
    return GetEnvironmentVariableW(L"MADEIRA_DOCK_FRIENDS", value, 4) == 1 && value[0] == L'0';
}

void sh_friends_init(struct sh_friends *f, HMODULE module, void *engine, int32_t user, int32_t pipe,
                     uint32_t appid, const struct dock_client_layout *layout, const struct sh_observer *o)
{
    memset(f, 0, sizeof *f);
    f->o = o;
    f->appid = appid;
    f->last_count = -1;
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
        {215, layout->friend_invite}, {194, layout->friend_join_lobby}, {195, layout->friend_join_presence}};
    for (unsigned i = 0; i < sizeof methods / sizeof methods[0]; ++i)
        if (!dock_method_is(base, friends, methods[i].slot, methods[i].rva)) {
            o->event("friends-available", 0);
            return;
        }
    f->friends = friends;
    f->layout = layout;
    o->event("friends-available", 1);
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
    for (int32_t i = 0; i < total && i < 2000 && count < SH_FRIENDS_MAX; ++i) {
        uint64_t id = 0;
        ((by_index_fn)SLOT(f, 10))(f->friends, &id, i, SH_FRIEND_FLAG_IMMEDIATE);
        int32_t state = sh_friend_steamid_valid(id) ? ((state_fn)SLOT(f, 13))(f->friends, id) : 0;
        if (state <= 0) continue;                   /* offline friends are left out */
        struct sh_friend *entry = &list[count++];
        memset(entry, 0, sizeof *entry);
        entry->steamid = id;
        entry->state = (uint32_t)state;
        sh_friend_name_copy(entry->name, sizeof entry->name, ((name_fn)SLOT(f, 14))(f->friends, id));
        struct game_info game = {0};
        if (((game_fn)SLOT(f, 23))(f->friends, id, &game) && game.game) {
            entry->flags |= SH_FRIEND_IN_GAME;
            entry->appid = game_app(&game);
            if (entry->appid && entry->appid == f->appid) {
                char connect[SH_FRIEND_CONNECT_CAP];
                entry->flags |= SH_FRIEND_SAME_APP;
                if (game.lobby || friend_connect(f, id, connect)) entry->flags |= SH_FRIEND_JOINABLE;
            }
        }
    }
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
    if (!friend_known(f, c->steamid)) return SH_FRIEND_UNKNOWN;
    if (c->op == SH_FRIEND_INVITE) {
        if (!game_running) return SH_FRIEND_NOT_JOINABLE;
        return ((invite_fn)SLOT(f, 215))(f->friends, f->appid, c->steamid, "") ? SH_FRIEND_DONE : SH_FRIEND_CALL_FAILED;
    }
    struct game_info game = {0};
    if (!((game_fn)SLOT(f, 23))(f->friends, c->steamid, &game) || game_app(&game) != f->appid)
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
    if (!f->friends || now < f->next_feed_ms) return;
    f->next_feed_ms = now + SH_FRIENDS_FEED_MS;
    write_feed(f);
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
                     uint32_t appid, const struct dock_client_layout *layout, const struct sh_observer *o)
{
    (void)module; (void)engine; (void)user; (void)pipe; (void)appid; (void)layout;
    memset(f, 0, sizeof *f);
    f->o = o;
    o->event("friends-available", 0);
}
void sh_friends_poll(struct sh_friends *f, bool game_running) { (void)f; (void)game_running; }
void sh_friends_end(struct sh_friends *f) { (void)f; }
#endif
