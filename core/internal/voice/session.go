// derived from discordo (github.com/ayn2op/discordo), gpl-3.0

//go:build !no_voice

package voice

import (
	"bytes"
	"context"
	"encoding/binary"
	"errors"
	"log/slog"
	"maps"
	"math"
	"sync"
	"sync/atomic"
	"time"
	"unsafe"

	"github.com/ayn2op/arikawa/v3/discord"
	"github.com/ayn2op/arikawa/v3/gateway"
	"github.com/ayn2op/arikawa/v3/state"
	"github.com/ayn2op/arikawa/v3/utils/ws"
	"github.com/ayn2op/arikawa/v3/voice"
	"github.com/ayn2op/arikawa/v3/voice/udp"
	"github.com/ayn2op/arikawa/v3/voice/voicegateway"
	"github.com/gen2brain/malgo"
	dave "github.com/thomas-vilte/dave-go/session"
	"github.com/tphakala/go-opus/opus"
)

const (
	timeout = 15 * time.Second
	// hangover is how many frames are still sent after the last that was loud enough, for the ends of words not to be cut off.
	hangover = 15
)

// audio is what the audio devices are opened with. It is of the first backend that works but the null one, which malgo would otherwise silently use when the system has no audio library.
var audio = sync.OnceValues(func() (*malgo.AllocatedContext, error) {
	return malgo.InitContext([]malgo.Backend{
		malgo.BackendWasapi, malgo.BackendDsound, malgo.BackendWinmm,
		malgo.BackendCoreaudio,
		malgo.BackendSndio, malgo.BackendAudio4, malgo.BackendOss,
		malgo.BackendPulseaudio, malgo.BackendAlsa, malgo.BackendJack,
	}, malgo.ContextConfig{}, nil)
})

// startDevice starts a playback or capture device, which calls data with what to fill or with what was captured.
func startDevice(kind malgo.DeviceType, channels int, deviceID unsafe.Pointer, data func(output, input []byte)) (*malgo.Device, error) {
	ctx, err := audio()
	if err != nil {
		return nil, err
	}

	config := malgo.DefaultDeviceConfig(kind)
	config.SampleRate = sampleRate
	config.PeriodSizeInMilliseconds = 10
	config.Playback.Format, config.Playback.Channels = malgo.FormatS16, uint32(channels)
	config.Capture.Format, config.Capture.Channels = malgo.FormatS16, uint32(channels)
	if kind == malgo.Capture {
		config.Capture.DeviceID = deviceID
	} else {
		config.Playback.DeviceID = deviceID
	}

	device, err := malgo.InitDevice(ctx.Context, config, malgo.DeviceCallbacks{
		Data: func(output, input []byte, _ uint32) { data(output, input) },
	})
	if err != nil {
		return nil, err
	}

	if err := device.Start(); err != nil {
		device.Uninit()
		return nil, err
	}

	return device, nil
}

// Session is the voice channel that the user is in, if any.
type Session struct {
	state *state.State
	// threshold is how loud a sample of the microphone must be for what is said to be sent.
	threshold atomic.Int64
	// updates is sent to when the status or who is speaking changes.
	updates chan struct{}
	// mic is sent what the microphone captures.
	mic chan []byte

	// mu is held while joining, leaving and muting, which are not done while another is.
	mu sync.Mutex
	// session is nil until the first voice channel is joined.
	session atomic.Pointer[voice.Session]
	// playback plays while a voice channel is joined, and capture captures while the user is not muted in it.
	playback, capture *malgo.Device
	// inputID and outputID are the devices picked by name, nil for the default ones.
	inputID, outputID unsafe.Pointer

	stateMu  sync.Mutex
	status   Status
	speaking map[discord.UserID]bool
	speakers map[uint32]*speaker
}

// New returns a Session that sends what the microphone captures louder than sensitivity, in dB.
func New(state *state.State, sensitivity float64) *Session {
	s := &Session{
		state:    state,
		updates:  make(chan struct{}, 1),
		mic:      make(chan []byte, 4),
		speaking: make(map[discord.UserID]bool),
		speakers: make(map[uint32]*speaker),
	}
	s.SetThreshold(sensitivity)
	state.AddHandler(s.follow)

	return s
}

// follow keeps up with the user being moved to another voice channel or disconnected, by someone else or by connecting from another client.
func (s *Session) follow(event *gateway.VoiceStateUpdateEvent) {
	me, _ := s.state.Cabinet.Me()
	if me == nil || event.UserID != me.ID {
		return
	}

	s.mu.Lock()
	defer s.mu.Unlock()

	status, own := s.Status(), event.SessionID == s.state.Ready().SessionID
	switch {
	case !status.Joined || own && event.ChannelID == status.ChannelID:
	case own && event.ChannelID.IsValid():
		s.setStatus(Status{ChannelID: event.ChannelID, Joined: true})
	default:
		s.left()
	}
}

// Updates is sent to when the status or who is speaking changes.
func (s *Session) Updates() <-chan struct{} {
	return s.updates
}

func (s *Session) Status() Status {
	s.stateMu.Lock()
	defer s.stateMu.Unlock()

	return s.status
}

// Speaking reports whether the user is speaking in the voice channel joined.
func (s *Session) Speaking(userID discord.UserID) bool {
	s.stateMu.Lock()
	defer s.stateMu.Unlock()

	return s.speaking[userID]
}

// Latency returns how long the voice server of the voice channel joined takes to answer, or 0 if it is not known.
func (s *Session) Latency() time.Duration {
	if session := s.session.Load(); session != nil {
		return session.Latency()
	}

	return 0
}

// update changes the state with f and tells Updates if f reports that it did.
func (s *Session) update(f func() bool) {
	s.stateMu.Lock()
	changed := f()
	s.stateMu.Unlock()

	if changed {
		select {
		case s.updates <- struct{}{}:
		default:
		}
	}
}

// setStatus changes the status, which whoever spoke and was heard does not outlast.
func (s *Session) setStatus(status Status) {
	s.update(func() bool {
		s.status = status
		clear(s.speaking)
		clear(s.speakers)
		return true
	})
}

func (s *Session) setSpeaking(userID discord.UserID, speaking bool) {
	s.update(func() bool {
		changed := s.speaking[userID] != speaking
		s.speaking[userID] = speaking
		return changed
	})
}

// setDevice starts device, which then calls data, if on, and stops it otherwise.
func setDevice(device **malgo.Device, on bool, kind malgo.DeviceType, deviceID unsafe.Pointer, data func(output, input []byte)) error {
	switch {
	case on && *device == nil:
		var err error
		*device, err = startDevice(kind, channels, deviceID, data)
		return err
	case !on && *device != nil:
		(*device).Uninit()
		*device = nil
	}

	return nil
}

// setDevices captures what the user says unless mute, and plays what is said unless deafen.
func (s *Session) setDevices(mute, deafen bool) error {
	err := errors.Join(
		setDevice(&s.capture, !mute, malgo.Capture, s.inputID, func(_, input []byte) { s.queue(bytes.Clone(input)) }),
		setDevice(&s.playback, !deafen, malgo.Playback, s.outputID, s.mix),
	)
	if mute {
		// Silence makes what is being said end.
		s.queue(make([]byte, 2*hangover*frameBytes))
	}

	return err
}

// left stops the devices, in no voice channel.
func (s *Session) left() {
	if err := s.setDevices(true, true); err != nil {
		slog.Error("failed to stop voice devices", "err", err)
	}
	s.setStatus(Status{})
}

// Join joins the voice channel with the self mute and deafen given, leaving the one that the user is in.
func (s *Session) Join(channelID discord.ChannelID, mute, deafen bool) error {
	if !s.mu.TryLock() {
		return nil
	}
	defer s.mu.Unlock()

	s.setStatus(Status{ChannelID: channelID})

	if err := s.join(channelID, mute, deafen); err != nil {
		// Discord may have the user in the voice channel regardless, and joining it again would then do nothing.
		return errors.Join(err, s.leave())
	}

	s.setStatus(Status{ChannelID: channelID, Joined: true})
	return nil
}

func (s *Session) join(channelID discord.ChannelID, mute, deafen bool) error {
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	session := s.session.Load()
	if session == nil {
		var err error
		if session, err = voice.NewSession(s.state, s.state.Handler); err != nil {
			return err
		}

		session.DAVE = dave.CreateFunc()
		session.AddHandler(func(event *ws.BackgroundErrorEvent) { slog.Debug("voice gateway error", "err", event.Err) })
		s.session.Store(session)

		go s.receive(session)
		go s.send(session)
	}

	if err := s.setDevices(mute || deafen, deafen); err != nil {
		return err
	}

	if err := session.JoinChannel(ctx, channelID, mute || deafen, deafen); err != nil {
		return err
	}

	// The voice server sends nothing before it is told this.
	if err := session.Speaking(ctx, voicegateway.Microphone); err != nil {
		return err
	}

	return nil
}

// Leave leaves the voice channel that the user is in, if any.
func (s *Session) Leave() error {
	if !s.mu.TryLock() {
		return nil
	}
	defer s.mu.Unlock()

	return s.leave()
}

func (s *Session) leave() error {
	defer s.left()

	session := s.session.Load()
	if session == nil {
		return nil
	}

	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	return session.Leave(ctx)
}

// SetSelf sets the self mute and deafen of the user in the voice channel joined, if any. deafening also mutes.
func (s *Session) SetSelf(mute, deafen bool) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	status := s.Status()
	channel, err := s.state.Cabinet.Channel(status.ChannelID)
	if err != nil || !status.Joined {
		return nil
	}

	mute = mute || deafen
	if err := s.setDevices(mute, deafen); err != nil {
		return err
	}

	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	return s.state.SendGateway(ctx, &gateway.UpdateVoiceStateCommand{
		GuildID:   channel.GuildID,
		ChannelID: channel.ID,
		SelfMute:  mute,
		SelfDeaf:  deafen,
	})
}

// SetThreshold sets how loud, in db, the microphone must be for what is said to be sent.
func (s *Session) SetThreshold(sensitivity float64) {
	s.threshold.Store(int64(math.Pow(10, sensitivity/20) * math.MaxInt16))
}

// Devices returns the names of the capture and playback devices.
func Devices() (inputs, outputs []string, err error) {
	ctx, err := audio()
	if err != nil {
		return nil, nil, err
	}

	for _, kind := range []malgo.DeviceType{malgo.Capture, malgo.Playback} {
		infos, err := ctx.Devices(kind)
		if err != nil {
			return nil, nil, err
		}
		for _, info := range infos {
			if kind == malgo.Capture {
				inputs = append(inputs, info.Name())
			} else {
				outputs = append(outputs, info.Name())
			}
		}
	}

	return inputs, outputs, nil
}

// deviceID returns the id of the device of kind named name, nil for "" or the default one.
func deviceID(kind malgo.DeviceType, name string) (unsafe.Pointer, error) {
	if name == "" {
		return nil, nil
	}

	ctx, err := audio()
	if err != nil {
		return nil, err
	}

	infos, err := ctx.Devices(kind)
	if err != nil {
		return nil, err
	}
	for _, info := range infos {
		if info.Name() == name {
			return info.ID.Pointer(), nil
		}
	}

	return nil, errors.New("no audio device named " + name)
}

// SetDevices picks the capture and playback devices by name, nil leaving one as it is and "" picking the default, restarting those that run.
func (s *Session) SetDevices(input, output *string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	var errs []error
	if input != nil {
		id, err := deviceID(malgo.Capture, *input)
		if err == nil && s.capture != nil {
			s.capture.Uninit()
			s.capture = nil
			s.inputID = id
			err = setDevice(&s.capture, true, malgo.Capture, s.inputID, func(_, input []byte) { s.queue(bytes.Clone(input)) })
		} else if err == nil {
			s.inputID = id
		}
		errs = append(errs, err)
	}
	if output != nil {
		id, err := deviceID(malgo.Playback, *output)
		if err == nil && s.playback != nil {
			s.playback.Uninit()
			s.playback = nil
			s.outputID = id
			err = setDevice(&s.playback, true, malgo.Playback, s.outputID, s.mix)
		} else if err == nil {
			s.outputID = id
		}
		errs = append(errs, err)
	}

	return errors.Join(errs...)
}

// Speakers returns who is speaking in the voice channel joined.
func (s *Session) Speakers() map[discord.UserID]bool {
	s.stateMu.Lock()
	defer s.stateMu.Unlock()

	return maps.Clone(s.speaking)
}

// queue queues what was captured to be sent, in place of the oldest if what is queued is not being sent fast enough, for what is said not to be sent ever later.
func (s *Session) queue(pcm []byte) {
	for {
		select {
		case s.mic <- pcm:
			return
		default:
		}

		select {
		case <-s.mic:
		default:
		}
	}
}

// mix fills output with what everyone says. It is what the playback device calls.
func (s *Session) mix(output, _ []byte) {
	s.stateMu.Lock()
	defer s.stateMu.Unlock()

	for _, speaker := range s.speakers {
		speaker.mix(output)
	}
}

// receive buffers what each user says in the voice channels that session joins.
func (s *Session) receive(session *voice.Session) {
	for {
		packet, err := session.ReadPacket()
		if errors.Is(err, udp.ErrDecryptionFailed) {
			continue
		}

		if err != nil {
			// In no voice channel, or joining one.
			time.Sleep(100 * time.Millisecond)
			continue
		}

		s.stateMu.Lock()
		speaker := s.speakers[packet.SSRC()]
		if speaker == nil {
			if speaker, err = newSpeaker(packet.Sequence()); err != nil {
				s.stateMu.Unlock()
				slog.Error("failed to create Opus decoder", "err", err)
				return
			}

			s.speakers[packet.SSRC()] = speaker
		}

		s.stateMu.Unlock()

		if err := speaker.write(packet.Sequence(), packet.Opus); err != nil {
			slog.Debug("failed to decode voice packet", "err", err, "ssrc", packet.SSRC())
		}

		s.setSpeaking(session.User(packet.SSRC()), string(packet.Opus) != voice.Silence)
	}
}

// send sends what the microphone captures to the voice channels that session joins, while it is loud enough.
func (s *Session) send(session *voice.Session) {
	encoder, err := opus.NewEncoder(opus.EncoderConfig{SampleRate: sampleRate, Channels: channels})
	if err != nil {
		slog.Error("failed to create Opus encoder", "err", err)
		return
	}

	me, _ := s.state.Cabinet.Me()

	var (
		pcm     []byte
		samples = make([]int16, frameSamples)
		packet  = make([]byte, 4000)
		silence = []byte(voice.Silence)
		// quiet is how many frames ago the last that was loud enough was.
		quiet = hangover
	)

	for captured := range s.mic {
		for pcm = append(pcm, captured...); len(pcm) >= frameBytes; pcm = pcm[frameBytes:] {
			quiet++
			threshold := int(s.threshold.Load())
			for i := range samples {
				samples[i] = int16(binary.LittleEndian.Uint16(pcm[2*i:]))
				if int(samples[i]) >= threshold || -int(samples[i]) >= threshold {
					quiet = 0
				}
			}

			var err error
			switch {
			case quiet < hangover:
				var n int
				if n, err = encoder.Encode(samples, packet); err == nil {
					_, err = session.Write(packet[:n])
				}
			case quiet < hangover+5:
				// A few frames of silence are for what was said not to be interpolated with what is said next.
				_, err = session.Write(silence)
			}

			if err != nil {
				slog.Debug("failed to send voice packet", "err", err)
			}

			if me != nil {
				s.setSpeaking(me.ID, quiet < hangover)
			}
		}
	}
}
