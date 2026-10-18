package client

import (
	"os"
	"path/filepath"

	"context"
	"errors"
	"github.com/ayn2op/arikawa/v3/utils/sendpart"
	"log/slog"
	"slices"
	"strconv"

	"github.com/ayn2op/arikawa/v3/api"
	"github.com/ayn2op/arikawa/v3/discord"
	"github.com/ayn2op/arikawa/v3/gateway"
	"github.com/ayn2op/ningen/v3"

	"ts3cord/core/internal/proto"
)

func parseID[T ~uint64](s, field string) (T, error) {
	n, err := strconv.ParseUint(s, 10, 64)
	if err != nil || n == 0 {
		return 0, errors.New("invalid " + field)
	}
	return T(n), nil
}

func (c *Client) handleChat(conn *conn, cmd proto.Command) error {
	s := conn.state
	switch cmd.Cmd {
	case "open_guild":
		guildID, err := parseID[discord.GuildID](cmd.GuildID, "guild_id")
		if err != nil {
			return err
		}
		fields, err := snapshot(s, guildID, conn.memberCount(guildID))
		if err != nil {
			return err
		}
		go s.MemberState.Subscribe(guildID)
		c.out.Emit("guild_snapshot", fields)

	case "fetch_messages":
		channelID, err := parseID[discord.ChannelID](cmd.ChannelID, "channel_id")
		if err != nil {
			return err
		}
		var before discord.MessageID
		if cmd.Before != "" {
			if before, err = parseID[discord.MessageID](cmd.Before, "before"); err != nil {
				return err
			}
		}
		limit := cmd.Limit
		if limit <= 0 || limit > 100 {
			limit = 50
		}
		c.async(cmd.Cmd, func() error { return c.fetchMessages(conn, channelID, before, limit) })

	case "send_message":
		channelID, err := parseID[discord.ChannelID](cmd.ChannelID, "channel_id")
		if err != nil {
			return err
		}
		data := api.SendMessageData{Content: cmd.Content, Nonce: cmd.Nonce}
		if cmd.ReplyTo != "" {
			replyTo, err := parseID[discord.MessageID](cmd.ReplyTo, "reply_to")
			if err != nil {
				return err
			}
			data.Reference = &discord.MessageReference{MessageID: replyTo, ChannelID: channelID}
		}
		files := cmd.Files
		// the echo is the message_create of the gateway, which has the nonce.
		c.inOrder(cmd.Cmd, func() error {
			for _, path := range files {
				f, err := os.Open(path)
				if err != nil {
					return err
				}
				defer f.Close()
				data.Files = append(data.Files, sendpart.File{Name: filepath.Base(path), Reader: f})
			}
			_, err := s.SendMessageComplex(channelID, data)
			return err
		})

	case "edit_message":
		channelID, err := parseID[discord.ChannelID](cmd.ChannelID, "channel_id")
		if err != nil {
			return err
		}
		messageID, err := parseID[discord.MessageID](cmd.MessageID, "message_id")
		if err != nil {
			return err
		}
		c.inOrder(cmd.Cmd, func() error {
			_, err := s.EditMessage(channelID, messageID, cmd.Content)
			return err
		})

	case "delete_message":
		channelID, err := parseID[discord.ChannelID](cmd.ChannelID, "channel_id")
		if err != nil {
			return err
		}
		messageID, err := parseID[discord.MessageID](cmd.MessageID, "message_id")
		if err != nil {
			return err
		}
		c.inOrder(cmd.Cmd, func() error { return s.DeleteMessage(channelID, messageID, "") })

	case "typing":
		channelID, err := parseID[discord.ChannelID](cmd.ChannelID, "channel_id")
		if err != nil {
			return err
		}
		c.async(cmd.Cmd, func() error { return s.Typing(channelID) })

	case "ack":
		channelID, err := parseID[discord.ChannelID](cmd.ChannelID, "channel_id")
		if err != nil {
			return err
		}
		messageID, err := parseID[discord.MessageID](cmd.MessageID, "message_id")
		if err != nil {
			return err
		}
		// mark read sends the ack and the unread event.
		go s.ReadState.MarkRead(channelID, messageID)

	case "join_voice":
		channelID, err := parseID[discord.ChannelID](cmd.ChannelID, "channel_id")
		if err != nil {
			return err
		}
		return c.joinVoice(conn, channelID)

	case "leave_voice":
		c.async(cmd.Cmd, conn.voice.Leave)

	case "set_self_mute", "set_self_deaf":
		c.mu.Lock()
		if cmd.Cmd == "set_self_mute" {
			c.selfMute = cmd.Mute
			if !cmd.Mute {
				c.selfDeaf = false
			}
		} else {
			c.selfDeaf = cmd.Deaf
			if cmd.Deaf {
				c.selfMute = true
			}
		}
		mute, deaf := c.selfMute, c.selfDeaf
		c.mu.Unlock()

		if !conn.voice.Status().Joined {
			c.out.Emit("voice_state", proto.F{"voice_state": selfVoiceState(conn, mute, deaf)})
			return nil
		}
		// the voice_state is the echo of the gateway.
		c.async(cmd.Cmd, func() error { return conn.voice.SetSelf(mute, deaf) })

	case "set_status":
		status := discord.Status(cmd.Status)
		switch status {
		case discord.OnlineStatus, discord.IdleStatus, discord.DoNotDisturbStatus, discord.InvisibleStatus:
		default:
			return errors.New("unknown status")
		}
		c.async(cmd.Cmd, func() error {
			if err := s.SetStatus(status, nil); err != nil {
				return err
			}
			me, err := s.Cabinet.Me()
			if err != nil {
				return err
			}
			act := ""
			if p, err := s.PresenceStore.Presence(discord.NullGuildID, me.ID); err == nil {
				act = activity(p.Activities)
			}
			c.out.Emit("presence", proto.F{"user_id": me.ID.String(), "status": string(status), "activity": act})
			return nil
		})

	case "user_profile":
		userID, err := parseID[discord.UserID](cmd.UserID, "user_id")
		if err != nil {
			return err
		}
		var guildID discord.GuildID
		if cmd.GuildID != "" {
			if guildID, err = parseID[discord.GuildID](cmd.GuildID, "guild_id"); err != nil {
				return err
			}
		}
		c.async(cmd.Cmd, func() error { return c.userProfile(conn, userID, guildID) })

	case "open_dm":
		userID, err := parseID[discord.UserID](cmd.UserID, "user_id")
		if err != nil {
			return err
		}
		c.async(cmd.Cmd, func() error {
			ch, err := s.CreatePrivateChannel(userID)
			if err != nil {
				return err
			}
			c.out.Emit("dm_open", proto.F{"channel": channel(s, *ch)})
			return nil
		})
	}
	return nil
}

func (c *Client) fetchMessages(conn *conn, channelID discord.ChannelID, before discord.MessageID, limit int) error {
	s := conn.state
	var msgs []discord.Message
	var err error
	if before.IsValid() {
		msgs, err = s.MessagesBefore(channelID, before, uint(limit))
	} else {
		msgs, err = s.Messages(channelID, uint(limit))
	}
	if err != nil {
		return err
	}
	if len(msgs) > limit {
		msgs = msgs[:limit]
	}

	// newest first from discord, newest last for the ui.
	slices.Reverse(msgs)
	out := make([]proto.Message, 0, len(msgs))
	for _, m := range msgs {
		out = append(out, message(s, m, nil))
	}
	c.out.Emit("messages", proto.F{"channel_id": channelID.String(), "messages": out, "has_more": len(msgs) == limit})

	if ch, err := s.Cabinet.Channel(channelID); err == nil && ch.GuildID.IsValid() {
		requestMembers(s, ch.GuildID, msgs)
	}
	return nil
}

// requestMembers asks the gateway for the members who wrote msgs and are not stored, for their names and colors.
func requestMembers(s *ningen.State, guildID discord.GuildID, msgs []discord.Message) {
	var userIDs []discord.UserID
	for _, m := range msgs {
		// a webhook message has no member.
		if m.WebhookID.IsValid() || slices.Contains(userIDs, m.Author.ID) {
			continue
		}
		if member(s, guildID, m.Author.ID) == nil {
			userIDs = append(userIDs, m.Author.ID)
		}
	}
	if len(userIDs) == 0 {
		return
	}

	if err := s.SendGateway(context.Background(), &gateway.RequestGuildMembersCommand{
		GuildIDs: []discord.GuildID{guildID},
		UserIDs:  userIDs,
	}); err != nil {
		slog.Error("failed to request guild members", "guild_id", guildID, "err", err)
	}
}

type profileResponse struct {
	User        discord.User `json:"user"`
	UserProfile struct {
		Bio string `json:"bio"`
	} `json:"user_profile"`
	GuildMember *discord.Member `json:"guild_member"`
	// premium_since is a nullable timestamp.
	PremiumSince *discord.Timestamp `json:"premium_since"`
}

func (c *Client) userProfile(conn *conn, userID discord.UserID, guildID discord.GuildID) error {
	s := conn.state

	url := api.EndpointUsers + userID.String() + "/profile?with_mutual_guilds=false&with_mutual_friends=false"
	if guildID.IsValid() {
		url += "&guild_id=" + guildID.String()
	}
	var resp profileResponse
	if err := s.RequestJSONInto(&resp, "GET", url); err != nil {
		// the stores still know enough to show something.
		slog.Warn("failed to fetch user profile", "user_id", userID, "err", err)
	}

	m := resp.GuildMember
	if m == nil {
		m = member(s, guildID, userID)
	}
	u := userByID(s, guildID, userID)
	if resp.User.ID.IsValid() {
		u = user(s, resp.User, guildID, m)
	}

	roles := []proto.ProfileRole{}
	joined, premium := "", ""
	if m != nil {
		joined = timestamp(m.Joined)
		premium = timestamp(m.BoostedSince)
		var rs []discord.Role
		for _, roleID := range m.RoleIDs {
			if r, err := s.Cabinet.Role(guildID, roleID); err == nil {
				rs = append(rs, *r)
			}
		}
		discord.SortRolesByPosition(rs)
		for _, r := range rs {
			roles = append(roles, proto.ProfileRole{Name: r.Name, Color: color(r.Color)})
		}
	}
	if resp.PremiumSince != nil {
		premium = timestamp(*resp.PremiumSince)
	}

	status, act := "offline", ""
	if p, err := s.Cabinet.Presence(guildID, userID); err == nil {
		status, act = string(p.Status), activity(p.Activities)
	} else if p, err := s.Cabinet.Presence(discord.NullGuildID, userID); err == nil {
		status, act = string(p.Status), activity(p.Activities)
	}

	bio := resp.UserProfile.Bio
	if bio == "" {
		bio = resp.User.Bio
	}

	c.out.Emit("user_profile", proto.F{
		"user":          u,
		"bio":           bio,
		"created_at":    userID.Time().UTC().Format("2006-01-02T15:04:05Z07:00"),
		"joined_at":     joined,
		"roles":         roles,
		"status":        status,
		"activity":      act,
		"premium_since": premium,
	})
	return nil
}
