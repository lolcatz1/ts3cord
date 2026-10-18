package client

import (
	"errors"
	"sync"
	"testing"

	"github.com/ayn2op/arikawa/v3/discord"

	"ts3cord/core/internal/proto"
	"ts3cord/core/internal/voice"
)

type recorder struct {
	mu  sync.Mutex
	evs []string
	f   []proto.F
}

func (r *recorder) Emit(ev string, fields proto.F) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.evs = append(r.evs, ev)
	r.f = append(r.f, fields)
}

func TestHandleLoggedOut(t *testing.T) {
	r := &recorder{}
	c := New(r)
	defer c.Close()

	if err := c.Handle(proto.Command{Cmd: "open_guild", GuildID: "1"}); !errors.Is(err, errNotLoggedIn) {
		t.Errorf("open_guild logged out: %v", err)
	}
	if err := c.Handle(proto.Command{Cmd: "login_token"}); err != nil {
		t.Fatal(err)
	}
	if len(r.evs) != 1 || r.evs[0] != "login_error" {
		t.Errorf("empty token gave %v", r.evs)
	}
}

func TestHandleOffline(t *testing.T) {
	r := &recorder{}
	c := New(r)
	s := offlineState(t)
	c.conn = &conn{state: s, voice: voice.New(s.State, -40), stop: make(chan struct{}), members: map[discord.GuildID]int{tGuild: 7}}
	defer c.Close()

	if err := c.Handle(proto.Command{Cmd: "open_guild", GuildID: "10"}); err != nil {
		t.Fatal(err)
	}
	if r.evs[0] != "guild_snapshot" || r.f[0]["guild"].(proto.GuildFull).MemberCount != 7 {
		t.Errorf("got %v %v", r.evs, r.f)
	}
	if err := c.Handle(proto.Command{Cmd: "open_guild", GuildID: "abc"}); err == nil {
		t.Error("bad id accepted")
	}
	if err := c.Handle(proto.Command{Cmd: "set_status", Status: "busy"}); err == nil {
		t.Error("bad status accepted")
	}

	// out of voice the self mute is answered at once.
	if err := c.Handle(proto.Command{Cmd: "set_self_deaf", Deaf: true}); err != nil {
		t.Fatal(err)
	}
	vs := r.f[len(r.f)-1]["voice_state"].(proto.VoiceState)
	if !vs.SelfDeaf || !vs.SelfMute || vs.User.ID != "1" || vs.ChannelID != "" {
		t.Errorf("voice state = %+v", vs)
	}

	// joining a voice channel without connect fails before any network.
	if err := c.Handle(proto.Command{Cmd: "join_voice", GuildID: "10", ChannelID: "102"}); err != nil {
		t.Fatal(err)
	}
	st := r.f[len(r.f)-1]
	if r.evs[len(r.evs)-1] != "voice_status" || st["state"] != "disconnected" || st["msg"] == "" {
		t.Errorf("voice_status = %v", st)
	}
}
