/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 125hz
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md
 * Bounded friends feed (host -> app) and command (app -> host) formats.
 * Portable: no Windows or Steam calls, so the parsers are unit-tested natively.
 */
#ifndef MADEIRA_DOCK_FRIENDS_FORMAT_H
#define MADEIRA_DOCK_FRIENDS_FORMAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SH_FRIENDS_MAX 250u
#define SH_FRIEND_NAME_CAP 64u          /* bytes including the terminator */
#define SH_FRIEND_CONNECT_CAP 256u      /* rich presence "connect", with terminator */
/* magic(8) count(4), then per friend: id(8) state(4) app(4) flags(1) name length(1) name */
#define SH_FRIENDS_FEED_CAP (12u + SH_FRIENDS_MAX * (18u + SH_FRIEND_NAME_CAP))
#define SH_FRIENDS_COMMAND_SIZE 21u     /* magic(8) op(1) id(8) sequence(4) */

enum sh_friend_flag {
    SH_FRIEND_IN_GAME = 1,      /* playing any game */
    SH_FRIEND_SAME_APP = 2,     /* playing this session's App ID */
    SH_FRIEND_JOINABLE = 4      /* same app with a lobby or a connect string */
};

enum sh_friend_op { SH_FRIEND_INVITE = 1, SH_FRIEND_JOIN = 2 };

/* friends-cmd-result values in the report. */
enum sh_friend_result {
    SH_FRIEND_DONE = 1,
    SH_FRIEND_NOT_JOINABLE = 2,
    SH_FRIEND_UNKNOWN = 3,
    SH_FRIEND_CALL_FAILED = 4,
    SH_FRIEND_BAD_COMMAND = 5,
    SH_FRIEND_UNAVAILABLE = 6
};

struct sh_friend {
    uint64_t steamid;
    uint32_t state, appid;
    uint8_t flags;
    char name[SH_FRIEND_NAME_CAP];
};

struct sh_friend_command {
    uint8_t op;
    uint64_t steamid;
    uint32_t sequence;
};

/* Copies at most cap-1 bytes of valid UTF-8, cutting at a character boundary
 * and dropping control characters. Invalid input yields an empty name. */
void sh_friend_name_copy(char *out, size_t cap, const char *in);
/* Printable ASCII only, 1..SH_FRIEND_CONNECT_CAP-1 bytes. */
bool sh_friend_connect_valid(const char *connect);
/* Individual SteamIDs only (universe public, account type individual). */
bool sh_friend_steamid_valid(uint64_t steamid);
/* Returns the encoded size, or 0 if out is too small or count exceeds the cap. */
size_t sh_friends_feed_encode(uint8_t *out, size_t cap, const struct sh_friend *friends, uint32_t count);
bool sh_friend_command_parse(const uint8_t *data, size_t size, struct sh_friend_command *command);
#endif
