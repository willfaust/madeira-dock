/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 125hz
 * Madeira Converter Exception: see LICENSE-EXCEPTION.md */
#include "friends_format.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Synthetic SteamIDs only: universe 1, individual, instance 1. */
#define ID(account) (0x0110000100000000ULL | (uint64_t)(account))

static size_t command(uint8_t *out, uint8_t op, uint64_t id, uint32_t sequence)
{
    memcpy(out, "MFCMD001", 8);
    out[8] = op;
    for (int i = 0; i < 8; ++i) out[9 + i] = (uint8_t)(id >> (8 * i));
    for (int i = 0; i < 4; ++i) out[17 + i] = (uint8_t)(sequence >> (8 * i));
    return SH_FRIENDS_COMMAND_SIZE;
}

int main(void)
{
    char name[SH_FRIEND_NAME_CAP];
    sh_friend_name_copy(name, sizeof name, "Player\tOne\n");
    assert(!strcmp(name, "PlayerOne"));
    sh_friend_name_copy(name, sizeof name, "caf\xc3\xa9");
    assert(!strcmp(name, "caf\xc3\xa9"));
    sh_friend_name_copy(name, sizeof name, "bad\xc3");
    assert(!name[0]);
    sh_friend_name_copy(name, sizeof name, "\xc0\xaf");   /* overlong */
    assert(!name[0]);
    sh_friend_name_copy(name, sizeof name, "\xed\xa0\x80"); /* surrogate */
    assert(!name[0]);
    sh_friend_name_copy(name, sizeof name, NULL);
    assert(!name[0]);
    char long_name[200];
    memset(long_name, 'a', 62); memcpy(long_name + 62, "\xc3\xa9", 3);
    sh_friend_name_copy(name, sizeof name, long_name);
    assert(strlen(name) == 62);                          /* never splits a character */
    char tiny[4];
    sh_friend_name_copy(tiny, sizeof tiny, "abcdef");
    assert(!strcmp(tiny, "abc"));

    assert(sh_friend_connect_valid("+connect_lobby 109775241000000000"));
    assert(!sh_friend_connect_valid(""));
    assert(!sh_friend_connect_valid(NULL));
    assert(!sh_friend_connect_valid("a\nb"));
    assert(!sh_friend_connect_valid("caf\xc3\xa9"));
    char connect[SH_FRIEND_CONNECT_CAP + 1];
    memset(connect, 'x', SH_FRIEND_CONNECT_CAP - 1); connect[SH_FRIEND_CONNECT_CAP - 1] = 0;
    assert(sh_friend_connect_valid(connect));
    memset(connect, 'x', SH_FRIEND_CONNECT_CAP); connect[SH_FRIEND_CONNECT_CAP] = 0;
    assert(!sh_friend_connect_valid(connect));

    assert(sh_friend_steamid_valid(ID(42)));
    assert(!sh_friend_steamid_valid(ID(0)));
    assert(!sh_friend_steamid_valid(0x0170000100000000ULL | 42)); /* clan */
    assert(!sh_friend_steamid_valid(0x0210000100000000ULL | 42)); /* universe 2 */

    struct sh_friend friends[2] = {
        {ID(1), 1, 0, 0, "One"},
        {ID(2), 1, 480, SH_FRIEND_IN_GAME | SH_FRIEND_SAME_APP | SH_FRIEND_JOINABLE | 0x80, "Two"}};
    static uint8_t feed[SH_FRIENDS_FEED_CAP];
    size_t size = sh_friends_feed_encode(feed, sizeof feed, friends, 2);
    assert(size == 12 + 2 * (18 + 3));
    assert(!memcmp(feed, "MFRND001", 8) && feed[8] == 2 && !feed[9]);
    assert(feed[12] == 1 && feed[12 + 16] == 0 && feed[12 + 17] == 3 && !memcmp(feed + 30, "One", 3));
    assert(feed[33 + 12] == (480 & 0xff) && feed[33 + 16] == 7);   /* unknown flag bit dropped */
    assert(!sh_friends_feed_encode(feed, size - 1, friends, 2));
    assert(!sh_friends_feed_encode(feed, sizeof feed, friends, SH_FRIENDS_MAX + 1));
    assert(sh_friends_feed_encode(feed, sizeof feed, NULL, 0) == 12);
    static struct sh_friend many[SH_FRIENDS_MAX];
    for (unsigned i = 0; i < SH_FRIENDS_MAX; ++i) {
        many[i].steamid = ID(i + 1);
        memset(many[i].name, 'n', SH_FRIEND_NAME_CAP - 1);
    }
    assert(sh_friends_feed_encode(feed, sizeof feed, many, SH_FRIENDS_MAX) == SH_FRIENDS_FEED_CAP - SH_FRIENDS_MAX);
    many[0].name[SH_FRIEND_NAME_CAP - 1] = 'n';            /* unterminated */
    assert(!sh_friends_feed_encode(feed, sizeof feed, many, 1));

    uint8_t raw[SH_FRIENDS_COMMAND_SIZE + 1];
    struct sh_friend_command parsed;
    size = command(raw, SH_FRIEND_JOIN, ID(7), 3);
    assert(sh_friend_command_parse(raw, size, &parsed));
    assert(parsed.op == SH_FRIEND_JOIN && parsed.steamid == ID(7) && parsed.sequence == 3);
    assert(sh_friend_command_parse(raw, command(raw, SH_FRIEND_INVITE, ID(7), 4), &parsed));
    assert(!sh_friend_command_parse(raw, size - 1, &parsed));
    assert(!sh_friend_command_parse(raw, size + 1, &parsed));
    assert(!sh_friend_command_parse(raw, command(raw, 3, ID(7), 3), &parsed));
    assert(!sh_friend_command_parse(raw, command(raw, SH_FRIEND_JOIN, ID(7), 0), &parsed));
    assert(!sh_friend_command_parse(raw, command(raw, SH_FRIEND_JOIN, 0x0170000100000007ULL, 3), &parsed));
    command(raw, SH_FRIEND_JOIN, ID(7), 3); raw[0] = 'X';
    assert(!sh_friend_command_parse(raw, size, &parsed));
    assert(!sh_friend_command_parse(NULL, size, &parsed));
    uint8_t msg[8 + 14 + SH_FRIENDS_MAX * 9];
    size = sh_cm_change_status(msg, sizeof msg, 1);
    static const uint8_t status[] = {0xcc, 0x02, 0, 0x80, 0, 0, 0, 0, 0x08, 1, 0x28, 0, 0x38, 1, 0x40, 0};
    assert(size == sizeof status && !memcmp(msg, status, size));
    assert(!sh_cm_change_status(msg, 10, 1));
    uint64_t ids[2] = {ID(5), ID(6)};
    size = sh_cm_request_friend_data(msg, sizeof msg, SH_CM_FRIEND_DATA_FLAGS, ids, 2);
    static const uint8_t request[] = {0x2f, 0x03, 0, 0x80, 0, 0, 0, 0, 0x08, 0xd2, 0x08};
    assert(size == sizeof request + 18 && !memcmp(msg, request, sizeof request));
    assert(msg[11] == 0x11 && msg[12] == 5 && msg[19] == 1 && msg[20] == 0x11 && msg[21] == 6);
    assert(!sh_cm_request_friend_data(msg, sizeof msg, 1, ids, SH_FRIENDS_MAX + 1));
    assert(!sh_cm_request_friend_data(msg, 20, 1, ids, 2));
    size = sh_cm_invite_to_lobby(msg, sizeof msg, 480, 0x0186000000000001ULL, ID(9));
    static const uint8_t invite[] = {0xdd, 0x19, 0, 0x80, 3, 0, 0, 0, 0x18, 0xe0, 0x03, 0x11, 1};
    assert(size == 8 + 3 + 18 && !memcmp(msg, invite, sizeof invite) && msg[19] == 0x01 && msg[20] == 0x19 && msg[21] == 9);
    assert(!sh_cm_invite_to_lobby(msg, sizeof msg, 480, 0, ID(9)));
    assert(!sh_cm_invite_to_lobby(msg, sizeof msg, 480, 1, 0x0170000100000009ULL));
    size = sh_cm_service_call(msg, sizeof msg, "Chat.RequestFriendPersonaStates#1");
    static const uint8_t call[] = {0x97, 0, 0, 0x80, 35, 0, 0, 0, 0x62, 33, 'C', 'h', 'a', 't', '.'};
    assert(size == 10 + 33 && !memcmp(msg, call, sizeof call) && msg[size - 1] == '1');
    assert(!sh_cm_service_call(msg, 20, "Chat.RequestFriendPersonaStates#1") && !sh_cm_service_call(msg, sizeof msg, ""));
    puts("PASS: friends feed bounds, name/connect sanitizing, SteamID and command validation, Friends sign-in messages");
}
