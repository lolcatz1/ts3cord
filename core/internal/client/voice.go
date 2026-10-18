// parts derived from discordo (github.com/ayn2op/discordo), gpl-3.0

package client

import (
	"errors"
	"log/slog"
	"time"

	"github.com/ayn2op/arikawa/v3/discord"

	"ts3cord/core/internal/proto"
	"ts3cord/core/internal/voice"
)

// pingEvery is how often voice_status repeats the ping while connected.
const pingEvery = 5 * time.Second

// joinable returns why the voice channel cannot be joined, or nil, as discordo checks it.
func joinable(conn *conn, ch *discord.Channel) error {
	s := conn.state
	switch {
	case ch.Type != discord.GuildVoice && ch.Type != discord.GuildStageVoice:
		return errors.New("not a voice channel")
	case !s.HasPermissions(ch.ID, discord.PermissionConnect|discord.PermissionViewChannel):
		return errors.New("you are not permitted to connect")
	}

	if ch.VoiceUserLimit > 0 && !s.HasPermissions(ch.ID, discord.PermissionMoveMembers) {
		vss, _ := s.Cabinet.VoiceStates(ch.GuildID)
		n := 0
		for _, vs := range vss {
			if vs.ChannelID == ch.ID {
				n++
			}
		}
		// discord does not answer a request to join a full voice channel.
		if n >= int(ch.VoiceUserLimit) {
			return errors.New("the channel is full")
		}
	}
	return nil
}

func (c *Client) joinVoice(conn *conn, channelID discord.ChannelID) error {
	ch, err := conn.state.Cabinet.Channel(channelID)
	if err != nil {
		return err
	}
	if err := joinable(conn, ch); err != nil {
		c.out.Emit("voice_status", proto.F{"state": "disconnected", "guild_id": id(ch.GuildID), "channel_id": ch.ID.String(), "ping_ms": 0, "msg": err.Error()})
		return nil
	}

	c.mu.Lock()
	mute, deaf := c.selfMute, c.selfDeaf
	c.mu.Unlock()

	// watchvoice emits the voice_status of each step.
	c.async("join_voice", func() error { return conn.voice.Join(ch.ID, mute, deaf) })
	return nil
}

// watchVoice emits voice_status and speaking as the voice session of conn changes, until conn is closed.
func (c *Client) watchVoice(conn *conn) {
	ticker := time.NewTicker(pingEvery)
	defer ticker.Stop()

	var last voice.Status
	var guildID discord.GuildID
	speaking := make(map[discord.UserID]bool)

	emitStatus := func(status voice.Status) {
		state := "disconnected"
		switch {
		case status.Joined:
			state = "connected"
		case status.ChannelID.IsValid():
			state = "connecting"
		}
		channelID := ""
		if status.ChannelID.IsValid() {
			channelID = status.ChannelID.String()
			if ch, err := conn.state.Cabinet.Channel(status.ChannelID); err == nil {
				guildID = ch.GuildID
			}
		}
		ping := 0
		if status.Joined {
			ping = int(conn.voice.Latency().Milliseconds())
		}
		c.out.Emit("voice_status", proto.F{"state": state, "guild_id": id(guildID), "channel_id": channelID, "ping_ms": ping, "msg": ""})
	}

	for {
		select {
		case <-conn.stop:
			return
		case <-ticker.C:
			if last.Joined {
				emitStatus(last)
			}
			continue
		case <-conn.voice.Updates():
		}

		status := conn.voice.Status()
		if status != last {
			// who spoke does not outlast the channel.
			for userID, on := range speaking {
				if on {
					c.out.Emit("speaking", proto.F{"guild_id": id(guildID), "user_id": userID.String(), "speaking": false})
				}
			}
			clear(speaking)
			emitStatus(status)
			last = status
		}

		for userID, on := range conn.voice.Speakers() {
			if !userID.IsValid() || speaking[userID] == on {
				continue
			}
			speaking[userID] = on
			c.out.Emit("speaking", proto.F{"guild_id": id(guildID), "user_id": userID.String(), "speaking": on})
		}
	}
}

func (c *Client) voiceSettings(cmd proto.Command) error {
	c.mu.Lock()
	if cmd.Threshold != nil {
		c.threshold = *cmd.Threshold
	}
	if cmd.InputDevice != nil {
		c.input = cmd.InputDevice
	}
	if cmd.OutputDevice != nil {
		c.output = cmd.OutputDevice
	}
	conn := c.conn
	c.mu.Unlock()

	var err error
	if conn != nil {
		if cmd.Threshold != nil {
			conn.voice.SetThreshold(*cmd.Threshold)
		}
		if cmd.InputDevice != nil || cmd.OutputDevice != nil {
			err = conn.voice.SetDevices(cmd.InputDevice, cmd.OutputDevice)
		}
	}

	inputs, outputs, derr := voice.Devices()
	if derr != nil {
		slog.Warn("failed to list audio devices", "err", derr)
	}
	if inputs == nil {
		inputs = []string{}
	}
	if outputs == nil {
		outputs = []string{}
	}
	c.out.Emit("voice_devices", proto.F{"inputs": inputs, "outputs": outputs})
	return err
}
