/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 125hz
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md */
#include "friends_format.h"
#include <string.h>

static const uint8_t feed_magic[8] = {'M', 'F', 'R', 'N', 'D', '0', '0', '1'};
static const uint8_t command_magic[8] = {'M', 'F', 'C', 'M', 'D', '0', '0', '1'};

/* Length of the UTF-8 sequence starting at s (at most `left` bytes), or 0. */
static size_t utf8_length(const unsigned char *s, size_t left)
{
    unsigned char c = s[0];
    size_t n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
    if (!n || n > left || (n == 2 && c < 0xc2) || (n == 4 && c > 0xf4)) return 0;
    for (size_t i = 1; i < n; ++i) if ((s[i] & 0xc0) != 0x80) return 0;
    if (n == 3 && ((c == 0xe0 && s[1] < 0xa0) || (c == 0xed && s[1] >= 0xa0))) return 0;
    if (n == 4 && ((c == 0xf0 && s[1] < 0x90) || (c == 0xf4 && s[1] >= 0x90))) return 0;
    return n;
}

void sh_friend_name_copy(char *out, size_t cap, const char *in)
{
    if (!out || !cap) return;
    out[0] = 0;
    if (!in) return;
    const unsigned char *s = (const unsigned char *)in;
    size_t length = strnlen(in, 4096), used = 0;
    for (size_t i = 0; i < length;) {
        size_t n = utf8_length(s + i, length - i);
        if (!n) { out[0] = 0; return; }
        if (!(n == 1 && (s[i] < 32 || s[i] == 127))) {
            if (used + n >= cap) break;
            memcpy(out + used, s + i, n);
            used += n;
        }
        i += n;
    }
    out[used] = 0;
}

bool sh_friend_connect_valid(const char *connect)
{
    if (!connect || !connect[0]) return false;
    size_t length = strnlen(connect, SH_FRIEND_CONNECT_CAP);
    if (length >= SH_FRIEND_CONNECT_CAP) return false;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)connect[i] < 32 || (unsigned char)connect[i] > 126) return false;
    return true;
}

bool sh_friend_steamid_valid(uint64_t steamid)
{
    return (steamid >> 56) == 1 && ((steamid >> 52) & 0xf) == 1 && (uint32_t)steamid != 0;
}

static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static void put64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i)); }

size_t sh_friends_feed_encode(uint8_t *out, size_t cap, const struct sh_friend *friends, uint32_t count)
{
    if (!out || count > SH_FRIENDS_MAX || (count && !friends) || cap < 12) return 0;
    memcpy(out, feed_magic, 8);
    put32(out + 8, count);
    size_t at = 12;
    for (uint32_t i = 0; i < count; ++i) {
        const struct sh_friend *f = &friends[i];
        size_t name = strnlen(f->name, SH_FRIEND_NAME_CAP);
        if (name >= SH_FRIEND_NAME_CAP || at + 18 + name > cap) return 0;
        put64(out + at, f->steamid);
        put32(out + at + 8, f->state);
        put32(out + at + 12, f->appid);
        out[at + 16] = f->flags & (SH_FRIEND_IN_GAME | SH_FRIEND_SAME_APP | SH_FRIEND_JOINABLE);
        out[at + 17] = (uint8_t)name;
        memcpy(out + at + 18, f->name, name);
        at += 18 + name;
    }
    return at;
}

bool sh_friend_command_parse(const uint8_t *data, size_t size, struct sh_friend_command *command)
{
    if (!data || !command || size != SH_FRIENDS_COMMAND_SIZE || memcmp(data, command_magic, 8)) return false;
    uint8_t op = data[8];
    uint64_t id = 0;
    uint32_t sequence = 0;
    for (int i = 7; i >= 0; --i) id = (id << 8) | data[9 + i];
    for (int i = 3; i >= 0; --i) sequence = (sequence << 8) | data[17 + i];
    if ((op != SH_FRIEND_INVITE && op != SH_FRIEND_JOIN) || !sh_friend_steamid_valid(id) || !sequence) return false;
    command->op = op;
    command->steamid = id;
    command->sequence = sequence;
    return true;
}
