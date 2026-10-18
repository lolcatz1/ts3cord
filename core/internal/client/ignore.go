// ts3cord core, gpl-3.0

package client

import (
	"log/slog"
	"maps"
	"time"

	"github.com/ayn2op/arikawa/v3/api"
	"github.com/ayn2op/arikawa/v3/discord"

	"ts3cord/core/internal/proto"
)

// relationship is the part of discord's relationship object that arikawa drops.
type relationship struct {
	ID          discord.UserID `json:"id"`
	Type        int            `json:"type"`
	UserIgnored bool           `json:"user_ignored"`
}

// relationshipsEvery re-reads the ignore flags; toggling ignore has no event arikawa forwards.
const relationshipsEvery = 3 * time.Minute

func (conn *conn) ignoredUsers() map[discord.UserID]bool {
	conn.ignoredMu.Lock()
	defer conn.ignoredMu.Unlock()
	return maps.Clone(conn.ignored)
}

// refreshRelationships fetches the ignore flags and emits relationships when they changed.
func (c *Client) refreshRelationships(conn *conn) {
	var rs []relationship
	if err := conn.state.RequestJSONInto(&rs, "GET", api.EndpointMe+"/relationships"); err != nil {
		slog.Warn("failed to fetch relationships", "err", err)
		return
	}
	ignored := make(map[discord.UserID]bool)
	for _, r := range rs {
		if r.UserIgnored {
			ignored[r.ID] = true
		}
	}
	conn.ignoredMu.Lock()
	same := maps.Equal(conn.ignored, ignored)
	conn.ignored = ignored
	conn.ignoredMu.Unlock()
	if !same {
		c.out.Emit("relationships", proto.F{"friends": friendList(conn.state, ignored)})
	}
}

// watchRelationships refreshes the ignore flags until conn is closed.
func (c *Client) watchRelationships(conn *conn) {
	t := time.NewTicker(relationshipsEvery)
	defer t.Stop()
	for {
		select {
		case <-conn.stop:
			return
		case <-t.C:
			c.refreshRelationships(conn)
		}
	}
}
