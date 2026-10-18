package client

import (
	"log/slog"

	"github.com/ayn2op/arikawa/v3/discord"
	"github.com/ayn2op/arikawa/v3/gateway"
	"github.com/ayn2op/arikawa/v3/utils/ws"
	"github.com/ayn2op/ningen/v3/states/read"

	"ts3cord/core/internal/proto"
)

// listen forwards the gateway events of conn until it is closed.
func (c *Client) listen(conn *conn, q *queue) {
	for {
		select {
		case <-conn.stop:
			return
		case <-q.signal:
		}
		for _, ev := range q.take() {
			select {
			case <-conn.stop:
				return
			default:
			}
			c.forward(conn, ev)
		}
	}
}

func (c *Client) forward(conn *conn, ev gateway.Event) {
	s := conn.state
	switch ev := ev.(type) {
	case *gateway.ReadyEvent:
		for _, g := range ev.Guilds {
			conn.setMemberCount(g.ID, g.MemberCount)
		}
		if conn.save {
			setToken(conn.token)
			conn.save = false
		}
		// with a prioritized ready, members, voice states and lazy dms come in ready_supplemental.
		if ev.Capabilities&gateway.PrioritizedReadyPayload == 0 {
			c.ready(conn)
		}

	case *gateway.ReadySupplementalEvent:
		c.ready(conn)
		// voice states of user accounts come here, for guilds the ui has open.
		for _, g := range ev.Guilds {
			for _, vs := range g.VoiceStates {
				vs.GuildID = g.ID
				if vs.ChannelID.IsValid() {
					c.out.Emit("voice_state", proto.F{"voice_state": voiceState(s, vs)})
				}
			}
		}

	case *gateway.RelationshipAddEvent, *gateway.RelationshipRemoveEvent:
		go c.refreshRelationships(conn)

	case *gateway.UserGuildSettingsUpdateEvent:
		// dm mutes live in the settings without a guild
		if !ev.GuildID.IsValid() {
			chs, _ := s.PrivateChannels()
			for _, ch := range chs {
				c.out.Emit("channel_update", proto.F{"channel": channel(s, ch)})
			}
		}

	case *gateway.ResumedEvent:
		c.out.Emit("connection", proto.F{"state": "connected", "msg": ""})

	case *ws.CloseEvent:
		state := "reconnecting"
		if ev.Code == closeCodeAuthenticationFailed {
			state = "disconnected"
		}
		c.out.Emit("connection", proto.F{"state": state, "msg": ev.Error()})

	case *gateway.GuildCreateEvent:
		conn.setMemberCount(ev.ID, ev.MemberCount)
		if ev.Unavailable {
			return
		}
		c.out.Emit("guild_create", proto.F{"guild": guildFull(ev.Guild, conn.memberCount(ev.ID))})
		for _, vs := range ev.VoiceStates {
			vs.GuildID = ev.ID
			if vs.ChannelID.IsValid() {
				c.out.Emit("voice_state", proto.F{"voice_state": voiceState(s, vs)})
			}
		}
	case *gateway.GuildUpdateEvent:
		c.out.Emit("guild_update", proto.F{"guild": guildFull(ev.Guild, conn.memberCount(ev.ID))})
	case *gateway.GuildDeleteEvent:
		c.out.Emit("guild_delete", proto.F{"guild_id": ev.ID.String()})

	case *gateway.ChannelCreateEvent:
		c.out.Emit("channel_create", proto.F{"channel": channel(s, ev.Channel)})
	case *gateway.ChannelUpdateEvent:
		c.out.Emit("channel_update", proto.F{"channel": channel(s, ev.Channel)})
	case *gateway.ChannelDeleteEvent:
		c.out.Emit("channel_delete", proto.F{"channel_id": ev.ID.String(), "guild_id": id(ev.GuildID)})

	case *gateway.MessageCreateEvent:
		c.out.Emit("message_create", proto.F{"message": message(s, ev.Message, ev.Member), "nonce": ev.Nonce})
	case *gateway.MessageUpdateEvent:
		// an update can be partial, the stored message is whole.
		m := ev.Message
		if stored, err := s.Cabinet.Message(ev.ChannelID, ev.ID); err == nil {
			m = *stored
		} else if !m.Author.ID.IsValid() {
			return
		}
		c.out.Emit("message_update", proto.F{"message": message(s, m, ev.Member)})
	case *gateway.MessageDeleteEvent:
		c.out.Emit("message_delete", proto.F{"channel_id": ev.ChannelID.String(), "message_id": ev.ID.String()})
	case *gateway.MessageDeleteBulkEvent:
		for _, msgID := range ev.IDs {
			c.out.Emit("message_delete", proto.F{"channel_id": ev.ChannelID.String(), "message_id": msgID.String()})
		}

	case *gateway.VoiceStateUpdateEvent:
		c.out.Emit("voice_state", proto.F{"voice_state": voiceState(s, ev.VoiceState)})

	case *gateway.PresenceUpdateEvent:
		c.out.Emit("presence", proto.F{"user_id": ev.User.ID.String(), "status": string(ev.Status), "activity": activity(ev.Activities)})

	case *gateway.TypingStartEvent:
		if me, err := s.Cabinet.Me(); err == nil && me.ID == ev.UserID {
			return
		}
		u := userByID(s, ev.GuildID, ev.UserID)
		if ev.Member != nil && ev.Member.User.ID.IsValid() {
			u = user(s, ev.Member.User, ev.GuildID, ev.Member)
		}
		c.out.Emit("typing", proto.F{"channel_id": ev.ChannelID.String(), "user": u})

	case *read.UpdateEvent:
		c.out.Emit("unread", proto.F{"channel_id": ev.ChannelID.String(), "unread": ev.Unread, "mentions": ev.MentionCount})
	}
}

func (c *Client) ready(conn *conn) {
	fields, err := readyFields(conn.state, conn.memberCounts(), conn.ignoredUsers())
	if err != nil {
		slog.Error("failed to build ready", "err", err)
		return
	}
	c.out.Emit("connection", proto.F{"state": "connected", "msg": ""})
	c.out.Emit("ready", fields)
	// the ignore flags come from the api, after ready
	go c.refreshRelationships(conn)
}

// selfVoiceState is the voice state of the user outside of voice, for the self mute and deafen.
func selfVoiceState(conn *conn, mute, deaf bool) proto.VoiceState {
	vs := proto.VoiceState{SelfMute: mute, SelfDeaf: deaf}
	if me, err := conn.state.Cabinet.Me(); err == nil {
		vs.User = user(conn.state, *me, discord.NullGuildID, nil)
	}
	return vs
}
