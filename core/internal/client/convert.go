package client

import (
	"cmp"
	"fmt"
	"regexp"
	"slices"
	"strconv"
	"time"

	"github.com/ayn2op/arikawa/v3/discord"
	"github.com/ayn2op/arikawa/v3/gateway"
	"github.com/ayn2op/arikawa/v3/state"
	"github.com/ayn2op/ningen/v3"

	"ts3cord/core/internal/proto"
)

// conversion of discord objects into protocol objects, against the ningen state.

const avatarSize = "?size=64"

func id[T interface {
	IsValid() bool
	String() string
}](v T) string {
	if !v.IsValid() {
		return ""
	}
	return v.String()
}

func timestamp(t discord.Timestamp) string {
	if !t.IsValid() {
		return ""
	}
	return t.Time().UTC().Format(time.RFC3339)
}

func color(c discord.Color) string {
	if c <= 0 {
		return ""
	}
	return fmt.Sprintf("#%06x", c.Uint32()&0xffffff)
}

func avatarURL(u discord.User) string {
	url := u.AvatarURLWithType(discord.PNGImage)
	if u.Avatar != "" {
		url += avatarSize
	}
	return url
}

func member(s *ningen.State, guildID discord.GuildID, userID discord.UserID) *discord.Member {
	if !guildID.IsValid() {
		return nil
	}
	m, _ := s.Cabinet.Member(guildID, userID)
	return m
}

// user converts u as shown in guildID, with m its member if known.
func user(s *ningen.State, u discord.User, guildID discord.GuildID, m *discord.Member) proto.User {
	if m == nil {
		m = member(s, guildID, u.ID)
	}
	display := u.DisplayOrUsername()
	if m != nil && m.Nick != "" {
		display = m.Nick
	}
	return proto.User{
		ID:         u.ID.String(),
		Username:   u.Username,
		GlobalName: u.DisplayName,
		Display:    display,
		AvatarURL:  avatarURL(u),
		Bot:        u.Bot,
	}
}

// userByID finds the user id in the member, user, presence and relationship stores.
func userByID(s *ningen.State, guildID discord.GuildID, userID discord.UserID) proto.User {
	if m := member(s, guildID, userID); m != nil {
		return user(s, m.User, guildID, m)
	}
	if u, err := s.Cabinet.User(userID); err == nil && u.Username != "" {
		return user(s, *u, guildID, nil)
	}
	if p, err := s.Cabinet.Presence(guildID, userID); err == nil && p.User.Username != "" {
		return user(s, p.User, guildID, nil)
	}
	if p, err := s.Cabinet.Presence(discord.NullGuildID, userID); err == nil && p.User.Username != "" {
		return user(s, p.User, guildID, nil)
	}
	if r, ok := s.RelationshipState.FullRelationship(userID); ok && r.User.Username != "" {
		return user(s, r.User, guildID, nil)
	}
	if me, err := s.Cabinet.Me(); err == nil && me.ID == userID {
		return user(s, *me, guildID, nil)
	}
	return proto.User{ID: userID.String(), Username: userID.String(), Display: userID.String(), AvatarURL: avatarURL(discord.User{ID: userID})}
}

func isDM(t discord.ChannelType) bool {
	return t == discord.DirectMessage || t == discord.GroupDM
}

func isThread(t discord.ChannelType) bool {
	return t == discord.GuildAnnouncementThread || t == discord.GuildPublicThread || t == discord.GuildPrivateThread
}

func channel(s *ningen.State, ch discord.Channel) proto.Channel {
	c := proto.Channel{
		ID:            ch.ID.String(),
		GuildID:       id(ch.GuildID),
		Type:          int(ch.Type),
		Name:          ch.Name,
		Position:      ch.Position,
		ParentID:      id(ch.ParentID),
		Topic:         ch.Topic,
		NSFW:          ch.NSFW,
		Bitrate:       int(ch.VoiceBitrate),
		UserLimit:     int(ch.VoiceUserLimit),
		LastMessageID: id(ch.LastMessageID),
	}

	if isDM(ch.Type) {
		c.CanView, c.CanSend, c.CanConnect = true, true, true
		c.Recipients = make([]proto.User, 0, len(ch.DMRecipients))
		for _, r := range ch.DMRecipients {
			c.Recipients = append(c.Recipients, user(s, r, discord.NullGuildID, nil))
		}
	} else if me, err := s.Cabinet.Me(); err == nil {
		if perms, err := s.Permissions(ch.ID, me.ID); err == nil {
			c.CanView = perms.Has(discord.PermissionViewChannel)
			c.CanSend = c.CanView && perms.Has(discord.PermissionSendMessages)
			c.CanConnect = c.CanView && perms.Has(discord.PermissionConnect)
		}
	}

	c.Unread = s.ChannelIsUnread(ch.ID, ningen.UnreadOpts{IncludeMutedCategories: true}) != ningen.ChannelRead
	c.Muted = s.MutedState.Channel(ch.ID)
	if rs := s.ReadState.ReadState(ch.ID); rs != nil {
		c.Mentions = rs.MentionCount
	}
	return c
}

func guildShort(g discord.Guild, members int) proto.Guild {
	return proto.Guild{ID: g.ID.String(), Name: g.Name, IconURL: guildIcon(g), OwnerID: id(g.OwnerID), MemberCount: members}
}

func guildIcon(g discord.Guild) string {
	if g.Icon == "" {
		return ""
	}
	return g.IconURLWithType(discord.PNGImage) + avatarSize
}

func guildFull(g discord.Guild, members int) proto.GuildFull {
	banner := ""
	if g.Banner != "" {
		banner = g.BannerURLWithType(discord.PNGImage)
	}
	return proto.GuildFull{
		ID:          g.ID.String(),
		Name:        g.Name,
		IconURL:     guildIcon(g),
		OwnerID:     id(g.OwnerID),
		Description: g.Description,
		MemberCount: members,
		BannerURL:   banner,
	}
}

func role(r discord.Role) proto.Role {
	return proto.Role{ID: r.ID.String(), Name: r.Name, Color: color(r.Color), Position: r.Position, Hoist: r.Hoist}
}

func voiceState(s *ningen.State, vs discord.VoiceState) proto.VoiceState {
	u := userByID(s, vs.GuildID, vs.UserID)
	if vs.Member != nil && vs.Member.User.ID.IsValid() {
		u = user(s, vs.Member.User, vs.GuildID, vs.Member)
	}
	return proto.VoiceState{
		GuildID:   id(vs.GuildID),
		ChannelID: id(vs.ChannelID),
		User:      u,
		SelfMute:  vs.SelfMute,
		SelfDeaf:  vs.SelfDeaf,
		Mute:      vs.Mute,
		Deaf:      vs.Deaf,
		Streaming: vs.SelfStream,
		Video:     vs.SelfVideo,
		Suppress:  vs.Suppress,
	}
}

// activity is the first activity as one line, the custom status last.
func activity(as []discord.Activity) string {
	custom := ""
	for _, a := range as {
		if a.Type == discord.CustomActivity {
			custom = cmp.Or(a.State, a.Name)
			continue
		}
		return a.Name
	}
	return custom
}

var mentionRegex = regexp.MustCompile(`<(@!?|@&|#)(\d+)>`)

// resolveMentions replaces user, role and channel mentions in the content of m with names.
func resolveMentions(s *ningen.State, content string, m *discord.Message, guildID discord.GuildID) string {
	return mentionRegex.ReplaceAllStringFunc(content, func(match string) string {
		sub := mentionRegex.FindStringSubmatch(match)
		n, err := strconv.ParseUint(sub[2], 10, 64)
		if err != nil {
			return match
		}
		switch sub[1] {
		case "@&":
			if r, err := s.Cabinet.Role(guildID, discord.RoleID(n)); err == nil {
				return "@" + r.Name
			}
			return "@deleted-role"
		case "#":
			if ch, err := s.Cabinet.Channel(discord.ChannelID(n)); err == nil {
				return "#" + ch.Name
			}
			return "#unknown"
		default:
			userID := discord.UserID(n)
			if m != nil {
				for _, u := range m.Mentions {
					if u.ID == userID {
						return "@" + user(s, u.User, guildID, u.Member).Display
					}
				}
			}
			return "@" + userByID(s, guildID, userID).Display
		}
	})
}

func messageGuild(s *ningen.State, m *discord.Message) discord.GuildID {
	if m.GuildID.IsValid() {
		return m.GuildID
	}
	if ch, err := s.Cabinet.Channel(m.ChannelID); err == nil {
		return ch.GuildID
	}
	return discord.NullGuildID
}

func message(s *ningen.State, m discord.Message, mem *discord.Member) proto.Message {
	guildID := messageGuild(s, &m)
	if mem == nil && !m.WebhookID.IsValid() {
		mem = member(s, guildID, m.Author.ID)
	}

	out := proto.Message{
		ID:              m.ID.String(),
		ChannelID:       m.ChannelID.String(),
		GuildID:         id(guildID),
		Type:            int(m.Type),
		Author:          user(s, m.Author, guildID, mem),
		Content:         resolveMentions(s, m.Content, &m, guildID),
		Timestamp:       timestamp(m.Timestamp),
		EditedTimestamp: timestamp(m.EditedTimestamp),
		MentionsMe:      s.MessageMentions(&m).Has(ningen.MessageMentions),
		Attachments:     make([]proto.Attachment, 0, len(m.Attachments)),
		Embeds:          make([]proto.Embed, 0, len(m.Embeds)),
	}

	if mem != nil {
		c, _ := state.MemberColor(mem, func(id discord.RoleID) *discord.Role {
			r, _ := s.Cabinet.Role(guildID, id)
			return r
		})
		out.AuthorColor = color(c)
	}

	for _, a := range m.Attachments {
		out.Attachments = append(out.Attachments, proto.Attachment{
			Filename:    a.Filename,
			URL:         a.URL,
			Size:        int(a.Size),
			ContentType: a.ContentType,
			Width:       int(a.Width),
			Height:      int(a.Height),
		})
	}

	for _, e := range m.Embeds {
		pe := proto.Embed{Title: e.Title, Description: e.Description, URL: e.URL}
		if e.Author != nil {
			pe.Author = e.Author.Name
		}
		if e.Footer != nil {
			pe.Footer = e.Footer.Text
		}
		out.Embeds = append(out.Embeds, pe)
	}

	if ref := m.ReferencedMessage; ref != nil {
		out.ReplyTo = &proto.Reply{
			ID:      ref.ID.String(),
			Author:  user(s, ref.Author, guildID, nil),
			Content: resolveMentions(s, ref.Content, ref, guildID),
		}
	}

	return out
}

// snapshot is the guild_snapshot of guildID.
func snapshot(s *ningen.State, guildID discord.GuildID, members int) (proto.F, error) {
	g, err := s.Cabinet.Guild(guildID)
	if err != nil {
		return nil, err
	}

	chs, err := s.Cabinet.Channels(guildID)
	if err != nil {
		return nil, err
	}
	channels := make([]proto.Channel, 0, len(chs))
	for _, ch := range chs {
		if !isThread(ch.Type) {
			channels = append(channels, channel(s, ch))
		}
	}

	vss, _ := s.Cabinet.VoiceStates(guildID)
	voiceStates := make([]proto.VoiceState, 0, len(vss))
	for _, vs := range vss {
		vs.GuildID = guildID
		if vs.ChannelID.IsValid() {
			voiceStates = append(voiceStates, voiceState(s, vs))
		}
	}

	rs, _ := s.Cabinet.Roles(guildID)
	discord.SortRolesByPosition(rs)
	roles := make([]proto.Role, 0, len(rs))
	for _, r := range rs {
		roles = append(roles, role(r))
	}

	me := proto.Me{Roles: []string{}}
	if u, err := s.Cabinet.Me(); err == nil {
		if m := member(s, guildID, u.ID); m != nil {
			me.Nick = m.Nick
			for _, r := range m.RoleIDs {
				me.Roles = append(me.Roles, r.String())
			}
		}
	}

	return proto.F{
		"guild":        guildFull(*g, cmp.Or(members, int(g.ApproximateMembers))),
		"channels":     channels,
		"voice_states": voiceStates,
		"roles":        roles,
		"me":           me,
	}, nil
}

// readyFields are the fields of ready.
func readyFields(s *ningen.State, members map[discord.GuildID]int, ignored map[discord.UserID]bool) (proto.F, error) {
	me, err := s.Cabinet.Me()
	if err != nil {
		return nil, err
	}

	gs, _ := s.Cabinet.Guilds()
	guilds := make([]proto.Guild, 0, len(gs))
	for _, g := range gs {
		guilds = append(guilds, guildShort(g, cmp.Or(members[g.ID], int(g.ApproximateMembers))))
	}
	// the order of the user's guild folders, else by id.
	order := guildOrder(s.Ready())
	slices.SortStableFunc(guilds, func(a, b proto.Guild) int {
		return cmp.Compare(order[a.ID], order[b.ID])
	})

	chs, _ := s.PrivateChannels()
	dms := make([]proto.Channel, 0, len(chs))
	for _, ch := range chs {
		dms = append(dms, channel(s, ch))
	}

	friends := friendList(s, ignored)

	return proto.F{
		"user":    user(s, *me, discord.NullGuildID, nil),
		"guilds":  guilds,
		"dms":     dms,
		"friends": friends,
	}, nil
}

// friendList is every relationship of the user, with the ignore flag.
func friendList(s *ningen.State, ignored map[discord.UserID]bool) []proto.Friend {
	friends := []proto.Friend{}
	s.RelationshipState.Each(func(userID discord.UserID, t discord.RelationshipType) bool {
		u := proto.User{ID: userID.String()}
		if r, ok := s.RelationshipState.FullRelationship(userID); ok {
			u = user(s, r.User, discord.NullGuildID, nil)
		}
		friends = append(friends, proto.Friend{User: u, Type: int(t), Ignored: ignored[userID]})
		return false
	})
	// ignored users that are not friends or blocked still need to reach the ui
	for userID := range ignored {
		if _, ok := s.RelationshipState.FullRelationship(userID); !ok && s.RelationshipState.Relationship(userID) == 0 {
			friends = append(friends, proto.Friend{User: proto.User{ID: userID.String()}, Ignored: true})
		}
	}
	slices.SortFunc(friends, func(a, b proto.Friend) int { return cmp.Compare(a.User.ID, b.User.ID) })
	return friends
}

// guildOrder maps guild ids to their place in the guild folders of the user settings.
func guildOrder(r gateway.ReadyEvent) map[string]int {
	order := make(map[string]int)
	if r.UserSettings == nil {
		return order
	}
	i := 0
	for _, f := range r.UserSettings.GuildFolders {
		for _, g := range f.GuildIDs {
			order[g.String()] = i
			i++
		}
	}
	for k := range order {
		order[k] -= i
	}
	return order
}
