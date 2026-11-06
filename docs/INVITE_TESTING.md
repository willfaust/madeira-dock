# Testing invites

Status (details in `docs/HANDOFF.md`): the **lobby invite**
(`friends-invite-connect=3`) works and a friend accepted it. Sending an invite,
receiving one and joining with it, joining a friend without an invite, and being
joined without sending one have all been tested. **`+connect_lobby`** (2) was
tried before the lobby invite existed: the friend got a Join button but the game
did not join. The **connect string** (1) and the Invite button's visibility
rules have no recorded result.

## Setup

Two Steam accounts that are friends, both owning the game under test. Account A
runs the game through Madeira Dock; account B runs desktop Steam (or a second
Dock session). Read A's `C:\madeira-dock.txt` after each step. Reports carry
numbers only; keep SteamIDs and names out of anything you save.

## Cases

| Kind | Game needs | A does | Expect on A |
|---|---|---|---|
| Lobby invite | Lobby matchmaking | Create a lobby, open Friends, press Invite on B | Invite shown; `friends-own-game=4`, `friends-invite-connect=3`, `friends-cmd-result=1`. B gets a Steam invite and joining enters A's lobby |
| Connect string | Sets rich presence `connect` | Be in the game session that sets it, press Invite | `friends-invite-connect=1`, result 1. B's invite carries the connect string; accepting launches or joins with it |
| `+connect_lobby` | A lobby but no `connect` and no shared connection | As lobby invite, with friends sign-in off (set `MADEIRA_DOCK_FRIENDS_ONLINE=0` yourself; the app never does) | If the client knows the lobby (`friends-own-game=4`): `friends-invite-connect=2`, result 1. A session not signed in to Friends may not know it and sees friends as offline: then no Invite button, and a forced command returns result 2 (`friends-invite-connect=0`) |
| Nothing to join | Any | Main menu, no lobby, no `connect` | No Invite button; a forced command returns result 2 (`friends-invite-connect=0`) |
| Leaving | Lobby game | Leave the lobby | Invite disappears within about 6 s (the feed and the Friends page each refresh every 3 s) |
| Offline | Any | Choose Offline before launch | No Friends page; `friends-available=0` is the only friends report |

## Accept side

For each invite kind, accept on B and check the game joins. Also have B invite A
(Accept in the Friends page: `friends-cmd-result=1`, the game decides whether it
joins). Games that ignore lobby join callbacks, or that use
only `connect`, may behave differently: record the game's App ID and the
`friends-invite-connect` value, not the game's name, in `docs/HANDOFF.md`.

## Also check

Status changes from the Friends page (Online, Away, Invisible) show on B within
a few seconds, and an invite can still be sent after each change.
