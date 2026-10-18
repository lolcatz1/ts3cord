package client

import (
	"testing"

	"github.com/ayn2op/arikawa/v3/discord"
	"github.com/ayn2op/ningen/v3"

	"ts3cord/core/internal/proto"
)

const (
	tGuild   = discord.GuildID(10)
	tMe      = discord.UserID(1)
	tDave    = discord.UserID(2)
	tRed     = discord.RoleID(20)
	tBlue    = discord.RoleID(21)
	tNoColor = discord.RoleID(22)
)

// offlineState is a state made like a real one, with stores filled by hand. it never opens.
func offlineState(t *testing.T) *ningen.State {
	t.Helper()
	s := newState("offline")
	c := s.Cabinet

	must := func(err error) {
		t.Helper()
		if err != nil {
			t.Fatal(err)
		}
	}
	me := discord.User{ID: tMe, Username: "leroy", DisplayName: "Leroy"}
	dave := discord.User{ID: tDave, Username: "dave", Avatar: "abc"}
	must(c.MyselfSet(me, false))
	must(c.GuildSet(&discord.Guild{ID: tGuild, Name: "QC", OwnerID: 99, Icon: "ic"}, false))
	for _, r := range []discord.Role{
		{ID: discord.RoleID(tGuild), Name: "@everyone", Permissions: discord.PermissionViewChannel | discord.PermissionSendMessages | discord.PermissionConnect},
		{ID: tRed, Name: "Red", Color: 0xff0000, Position: 2},
		{ID: tBlue, Name: "Blue", Color: 0x0000ff, Position: 5},
		{ID: tNoColor, Name: "Plain", Position: 9},
	} {
		must(c.RoleSet(tGuild, &r, false))
	}
	must(c.MemberSet(tGuild, &discord.Member{User: me, Nick: "LJ", RoleIDs: []discord.RoleID{tRed}}, false))
	must(c.MemberSet(tGuild, &discord.Member{User: dave, RoleIDs: []discord.RoleID{tRed, tBlue, tNoColor}}, false))

	deny := func(p discord.Permissions) []discord.Overwrite {
		return []discord.Overwrite{{ID: discord.Snowflake(tGuild), Type: discord.OverwriteRole, Deny: p}}
	}
	for _, ch := range []discord.Channel{
		{ID: 100, GuildID: tGuild, Type: discord.GuildText, Name: "general"},
		{ID: 101, GuildID: tGuild, Type: discord.GuildText, Name: "secret", Overwrites: deny(discord.PermissionViewChannel)},
		{ID: 102, GuildID: tGuild, Type: discord.GuildVoice, Name: "Lobby", VoiceUserLimit: 1, Overwrites: deny(discord.PermissionConnect)},
		{ID: 103, GuildID: tGuild, Type: discord.GuildText, Name: "rules", Overwrites: deny(discord.PermissionSendMessages)},
		{ID: 104, GuildID: tGuild, Type: discord.GuildVoice, Name: "Open", VoiceUserLimit: 1},
	} {
		must(c.ChannelSet(&ch, false))
	}
	must(c.VoiceStateSet(tGuild, &discord.VoiceState{GuildID: tGuild, ChannelID: 104, UserID: tDave, SelfMute: true}, false))
	return s
}

func TestChannelPermissions(t *testing.T) {
	s := offlineState(t)
	tests := []struct {
		id                  discord.ChannelID
		view, send, connect bool
	}{
		{100, true, true, true},
		{101, false, false, false},
		{102, true, true, false},
		{103, true, false, true},
	}
	for _, test := range tests {
		ch, err := s.Cabinet.Channel(test.id)
		if err != nil {
			t.Fatal(err)
		}
		got := channel(s, *ch)
		if got.CanView != test.view || got.CanSend != test.send || got.CanConnect != test.connect {
			t.Errorf("%s: view %v send %v connect %v, want %v %v %v", got.Name, got.CanView, got.CanSend, got.CanConnect, test.view, test.send, test.connect)
		}
		if got.GuildID != "10" || got.ParentID != "" {
			t.Errorf("%s: guild %q parent %q", got.Name, got.GuildID, got.ParentID)
		}
	}
}

func TestMessage(t *testing.T) {
	s := offlineState(t)
	dave, _ := s.Cabinet.Member(tGuild, tDave)
	me, _ := s.Cabinet.Me()
	m := discord.Message{
		ID: 500, ChannelID: 100, Author: dave.User,
		Content:           "hi <@2> <@!1> <@&21> <#100> <#999> <@&888>",
		Mentions:          []discord.GuildUser{{User: dave.User}, {User: *me}},
		ReferencedMessage: &discord.Message{ID: 400, ChannelID: 100, Author: *me, Content: "yo <#103>"},
	}

	got := message(s, m, nil)
	if want := "hi @dave @LJ @Blue #general #unknown @deleted-role"; got.Content != want {
		t.Errorf("content = %q, want %q", got.Content, want)
	}
	if got.GuildID != "10" {
		t.Errorf("guild = %q, from the channel", got.GuildID)
	}
	if got.AuthorColor != "#0000ff" {
		t.Errorf("color = %q, want the highest colored role", got.AuthorColor)
	}
	if got.Author.Display != "dave" || got.Author.AvatarURL != "https://cdn.discordapp.com/avatars/2/abc.png?size=64" {
		t.Errorf("author = %+v", got.Author)
	}
	if !got.MentionsMe {
		t.Error("mention of me not noticed")
	}
	if got.ReplyTo == nil || got.ReplyTo.Author.Display != "LJ" || got.ReplyTo.Content != "yo #rules" {
		t.Errorf("reply = %+v", got.ReplyTo)
	}
	if got.Attachments == nil || got.Embeds == nil {
		t.Error("nil attachments or embeds")
	}
}

func TestSnapshot(t *testing.T) {
	s := offlineState(t)
	f, err := snapshot(s, tGuild, 42)
	if err != nil {
		t.Fatal(err)
	}
	if g := guildFull(discord.Guild{ID: tGuild, Name: "QC", Icon: "ic"}, 42); g.IconURL != "https://cdn.discordapp.com/icons/10/ic.png?size=64" {
		t.Errorf("icon = %q", g.IconURL)
	}
	vs := f["voice_states"]
	if n := len(vs.([]proto.VoiceState)); n != 1 {
		t.Fatalf("got %d voice states", n)
	}
	v := vs.([]proto.VoiceState)[0]
	if v.ChannelID != "104" || v.User.Display != "dave" || !v.SelfMute || v.GuildID != "10" {
		t.Errorf("voice state = %+v", v)
	}
	if roles := f["roles"].([]proto.Role); roles[0].Name != "Plain" || roles[1].Color != "#0000ff" {
		t.Errorf("roles = %+v", roles)
	}
	if me := f["me"].(proto.Me); me.Nick != "LJ" || len(me.Roles) != 1 {
		t.Errorf("me = %+v", me)
	}
}

func TestJoinable(t *testing.T) {
	s := offlineState(t)
	conn := &conn{state: s}
	for id, ok := range map[discord.ChannelID]bool{100: false, 102: false, 104: false} {
		ch, _ := s.Cabinet.Channel(id)
		if err := joinable(conn, ch); (err == nil) != ok {
			t.Errorf("%s: joinable err %v", ch.Name, err)
		}
	}
	// a free slot opens it.
	if err := s.Cabinet.VoiceStateRemove(tGuild, tDave); err != nil {
		t.Fatal(err)
	}
	ch, _ := s.Cabinet.Channel(104)
	if err := joinable(conn, ch); err != nil {
		t.Errorf("open channel: %v", err)
	}
}

func TestActivity(t *testing.T) {
	if got := activity([]discord.Activity{{Type: discord.CustomActivity, State: "afk"}, {Name: "Quake"}}); got != "Quake" {
		t.Errorf("got %q", got)
	}
	if got := activity([]discord.Activity{{Type: discord.CustomActivity, State: "afk"}}); got != "afk" {
		t.Errorf("got %q", got)
	}
}
