// derived from discordo (github.com/ayn2op/discordo), gpl-3.0

//go:build no_voice

package voice

import (
	"errors"
	"time"

	"github.com/ayn2op/arikawa/v3/discord"
	"github.com/ayn2op/arikawa/v3/state"
)

// Session joins no voice channel.
type Session struct{}

func New(*state.State, float64) *Session { return &Session{} }

func (*Session) Updates() <-chan struct{}     { return nil }
func (*Session) Status() Status               { return Status{} }
func (*Session) Speaking(discord.UserID) bool { return false }
func (*Session) Latency() time.Duration       { return 0 }
func (*Session) Leave() error                 { return nil }
func (*Session) SetSelf(bool, bool) error     { return nil }
func (*Session) SetThreshold(float64)         {}

func (*Session) SetDevices(*string, *string) error   { return nil }
func (*Session) Speakers() map[discord.UserID]bool   { return nil }
func Devices() (inputs, outputs []string, err error) { return nil, nil, errNoVoice }

var errNoVoice = errors.New("built without voice (build tag: no_voice)")

func (*Session) Join(discord.ChannelID, bool, bool) error { return errNoVoice }
