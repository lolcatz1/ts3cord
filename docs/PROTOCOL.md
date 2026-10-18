# ui <-> core protocol

core = `ts3cord-core` (go). ui spawns it, one json object per line.
stdin = commands (ui -> core), stdout = events (core -> ui), stderr = log text.
all ids are strings. timestamps are rfc3339. urls are absolute or "".

## shared objects

user
```
{"id","username","global_name","display","avatar_url","bot":bool}
```
display = guild nick > global_name > username, when a guild context exists.

channel
```
{"id","guild_id","type":int,"name","position":int,"parent_id","topic","nsfw":bool,
 "bitrate":int,"user_limit":int,"can_view":bool,"can_send":bool,"can_connect":bool,
 "last_message_id","unread":bool,"mentions":int,"muted":bool,"recipients":[user]}
```
type is the discord channel type number. recipients only for dms.

message
```
{"id","channel_id","guild_id","type":int,"author":user,"author_color":"#rrggbb"|"",
 "content","timestamp","edited_timestamp"|"","mentions_me":bool,
 "attachments":[{"filename","url","size":int,"content_type","width":int,"height":int}],
 "embeds":[{"title","description","url","author","footer"}],
 "reply_to":{"id","author":user,"content"}|null}
```
content is raw discord markdown with mentions resolved to `@name`, `#channel`, `@role`.

voice_state
```
{"guild_id","channel_id"|"","user":user,"self_mute","self_deaf","mute","deaf","streaming","video","suppress":bool}
```

## commands

| cmd | fields | reply |
|---|---|---|
| `start` | | `need_login` or session start (uses keyring token) |
| `login_token` | token | `ready` / `login_error` |
| `login_password` | login, password | `ready` / `mfa_required` / `login_error` |
| `login_mfa` | ticket, code, method ("totp") | `ready` / `login_error` |
| `qr_start` | | `qr`, then `qr_user`, then `ready` / `login_error` |
| `logout` | | `need_login` |
| `open_guild` | guild_id | `guild_snapshot` |
| `fetch_messages` | channel_id, before? , limit? | `messages` |
| `send_message` | channel_id, content, nonce, reply_to?, files? (local paths) | echo arrives as `message_create` with nonce |
| `edit_message` | channel_id, message_id, content | `message_update` |
| `delete_message` | channel_id, message_id | `message_delete` |
| `typing` | channel_id | |
| `ack` | channel_id, message_id | |
| `join_voice` | guild_id, channel_id | `voice_status` |
| `leave_voice` | | `voice_status` |
| `set_self_mute` | mute:bool | `voice_state` |
| `set_self_deaf` | deaf:bool | `voice_state` |
| `set_status` | status ("online","idle","dnd","invisible") | `presence` |
| `user_profile` | user_id, guild_id? | `user_profile` |
| `open_dm` | user_id | `dm_open` |
| `voice_settings` | input_device?, output_device?, threshold? | `voice_devices` |

## events

| ev | fields |
|---|---|
| `need_login` | |
| `login_error` | msg |
| `mfa_required` | ticket, methods:[string] |
| `qr` | url (encode as qr code in ui) |
| `qr_user` | username, avatar_url |
| `ready` | user, guilds:[{id,name,icon_url,owner_id,member_count}], dms:[channel], friends:[{user,type:int,ignored:bool}] |
| `relationships` | friends:[{user,type,ignored}] (ignore flags changed) |
| `connection` | state ("connecting","connected","reconnecting","disconnected"), msg |
| `guild_snapshot` | guild:{id,name,icon_url,owner_id,description,member_count,banner_url}, channels:[channel], voice_states:[voice_state], roles:[{id,name,color,position,hoist}], me:{nick,roles:[id]} |
| `guild_create` / `guild_update` | guild |
| `guild_delete` | guild_id |
| `channel_create` / `channel_update` | channel |
| `channel_delete` | channel_id, guild_id |
| `messages` | channel_id, messages:[message] (newest last), has_more:bool |
| `message_create` | message, nonce |
| `message_update` | message |
| `message_delete` | channel_id, message_id |
| `voice_state` | voice_state |
| `speaking` | guild_id, user_id, speaking:bool |
| `voice_status` | state ("connecting","connected","disconnected"), guild_id, channel_id, ping_ms:int, msg |
| `presence` | user_id, status, activity |
| `typing` | channel_id, user |
| `user_profile` | user, bio, created_at, joined_at, roles:[{name,color}], status, activity, premium_since |
| `dm_open` | channel |
| `unread` | channel_id, unread:bool, mentions:int |
| `voice_devices` | inputs:[string], outputs:[string] |
| `error` | msg, cmd |

## notes

- run: `ts3cord-core [--demo] [--debug]`. `--demo` = fake data, no network. core exits when stdin closes.
- every failed command answers `error` {msg, cmd}. unknown cmd -> msg "unknown command". a line that is not json -> cmd "".
- colors (roles, author_color, user_profile roles) are `"#rrggbb"`, `""` for none.
- `start`: token from the keyring (service "ts3cord"), or env `TS3CORD_TOKEN`. a rejected token -> `login_error` then `need_login`.
- `ready` comes after `connection` "connected", once the gateway sent ready_supplemental (members, voice states, lazy dms). it repeats after a full reconnect.
- `mfa_required` methods: any of "totp", "backup", "sms". `login_mfa` takes method "totp" or "backup" (sms is not supported).
- `guild_create` / `guild_update` guild has the `guild_snapshot` guild shape.
- `guild_snapshot` channels leave out threads. voice states for all guilds also arrive as `voice_state` events right after `ready`.
- `fetch_messages`: limit defaults to 50, max 100. has_more = a full page came back.
- `join_voice` on a channel without connect permission, or full: `voice_status` "disconnected" with msg, nothing sent. other join failures: `error` with cmd "join_voice" and `voice_status` "disconnected".
- `voice_status` "connected" repeats every few seconds with a fresh ping_ms.
- `set_self_mute` false also undeafens, `set_self_deaf` true also mutes (like discord). outside of voice the flags are kept for the next join and the reply `voice_state` has guild_id "" and channel_id "".
- `voice_settings` threshold is in dbfs (default -40). devices by name, "" = default. reply `voice_devices` lists the names.
- `typing` events never carry the user themselves.
- `user_profile` timestamps are rfc3339 or "". status "offline" when unknown.
- demo: `login_password` with password "mfa" answers `mfa_required` (ticket "demo-ticket", any 6 digit code works).
