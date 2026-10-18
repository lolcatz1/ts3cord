# ts3cord-core

discord backend of ts3cord. json lines over stdio, see ../docs/PROTOCOL.md.

    cd core && go build -o ts3cord-core .
    go build -tags no_voice -o ts3cord-core .   # no cgo audio, no voice
    ./ts3cord-core --demo                       # fake data, no network
    ./ts3cord-core --debug                      # debug logs on stderr

tags: `no_voice` (no malgo / opus), `no_spoof_tls_fingerprint` (plain net/http and websocket).

    internal/proto    protocol types, encoder, stdin loop
    internal/client   discord session (ningen), logins, voice
    internal/demo     --demo world (data.go)
    internal/http, internal/tls, internal/gateway, internal/voice   from discordo

## license

gpl-3.0. much of this is derived from discordo (https://github.com/ayn2op/discordo) by ayn2op, gpl-3.0:
internal/http, internal/tls, internal/gateway, internal/voice, the session setup in internal/client
and the remote auth login in internal/client/remoteauth.go. the core is gpl-3.0 as well.
