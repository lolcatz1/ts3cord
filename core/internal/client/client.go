// parts derived from discordo (github.com/ayn2op/discordo), gpl-3.0

// Package client answers the protocol with a discord session, set up the way discordo sets up its own.
package client

import (
	"context"
	"errors"
	"log/slog"
	"os"
	"sync"
	"time"

	"github.com/ayn2op/arikawa/v3/discord"
	"github.com/ayn2op/arikawa/v3/gateway"
	"github.com/ayn2op/arikawa/v3/session"
	"github.com/ayn2op/arikawa/v3/state"
	"github.com/ayn2op/arikawa/v3/state/store/defaultstore"
	"github.com/ayn2op/arikawa/v3/utils/handler"
	"github.com/ayn2op/arikawa/v3/utils/httputil"
	"github.com/ayn2op/arikawa/v3/utils/httputil/httpdriver"
	"github.com/ayn2op/arikawa/v3/utils/ws"
	"github.com/ayn2op/ningen/v3"

	clientgateway "ts3cord/core/internal/gateway"
	"ts3cord/core/internal/http"
	"ts3cord/core/internal/proto"
	"ts3cord/core/internal/voice"
)

// closeCodeAuthenticationFailed is the close code of the gateway for a token it rejects.
const closeCodeAuthenticationFailed = 4004

const tokenEnvVarKey = "TS3CORD_TOKEN"

const closeTimeout = 3 * time.Second

type Client struct {
	out proto.Emitter

	mu   sync.Mutex
	conn *conn
	// cancelLogin cancels the qr login running, if any.
	cancelLogin context.CancelFunc
	// mfa maps mfa tickets to their login instance ids.
	mfa map[string]string

	// self mute and deafen, kept out of voice too.
	selfMute, selfDeaf bool
	threshold          float64
	input, output      *string

	// ordered runs the commands that change messages one at a time, in order.
	ordered chan func()
	done    chan struct{}
	wg      sync.WaitGroup
}

// conn is one logged in session.
type conn struct {
	state *ningen.State
	voice *voice.Session
	// save stores the token in the keyring once ready, for a token that came from a login.
	token string
	save  bool
	stop  chan struct{}
	// members are the member counts of guilds, which the guild store drops.
	membersMu sync.Mutex
	members   map[discord.GuildID]int
	// ignored users, from the raw relationships (arikawa drops user_ignored).
	ignoredMu sync.Mutex
	ignored   map[discord.UserID]bool
}

func New(out proto.Emitter) *Client {
	c := &Client{
		out:       out,
		mfa:       make(map[string]string),
		threshold: -40,
		ordered:   make(chan func(), 64),
		done:      make(chan struct{}),
	}
	c.wg.Go(func() {
		for {
			select {
			case <-c.done:
				return
			case f := <-c.ordered:
				f()
			}
		}
	})
	return c
}

func (c *Client) Close() {
	c.mu.Lock()
	if c.cancelLogin != nil {
		c.cancelLogin()
	}
	conn := c.conn
	c.conn = nil
	c.mu.Unlock()

	if conn != nil {
		conn.close()
	}
	close(c.done)

	// requests in flight are not worth hanging the exit for.
	waited := make(chan struct{})
	go func() {
		c.wg.Wait()
		close(waited)
	}()
	select {
	case <-waited:
	case <-time.After(closeTimeout):
		slog.Warn("exiting with requests in flight")
	}
}

// async runs f away from the stdin reader, emitting its error.
func (c *Client) async(cmd string, f func() error) {
	c.wg.Go(func() {
		if err := f(); err != nil {
			slog.Warn("command failed", "cmd", cmd, "err", err)
			proto.Error(c.out, cmd, err)
		}
	})
}

// inOrder runs f after the commands inOrder was given before, emitting its error.
func (c *Client) inOrder(cmd string, f func() error) {
	run := func() {
		if err := f(); err != nil {
			slog.Warn("command failed", "cmd", cmd, "err", err)
			proto.Error(c.out, cmd, err)
		}
	}
	select {
	case c.ordered <- run:
	case <-c.done:
	}
}

func (c *Client) current() *conn {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.conn
}

var errNotLoggedIn = errors.New("not logged in")

func (c *Client) Handle(cmd proto.Command) error {
	switch cmd.Cmd {
	case "start":
		c.start()
		return nil
	case "login_token":
		if cmd.Token == "" {
			c.out.Emit("login_error", proto.F{"msg": "token is empty"})
			return nil
		}
		c.connect(cmd.Token, true)
		return nil
	case "login_password":
		c.async(cmd.Cmd, func() error { c.loginPassword(cmd.Login, cmd.Password); return nil })
		return nil
	case "login_mfa":
		c.async(cmd.Cmd, func() error { c.loginMFA(cmd.Ticket, cmd.Code, cmd.Method); return nil })
		return nil
	case "qr_start":
		c.startQR()
		return nil
	case "logout":
		c.logout()
		return nil
	case "voice_settings":
		return c.voiceSettings(cmd)
	}

	conn := c.current()
	if conn == nil {
		return errNotLoggedIn
	}
	return c.handleChat(conn, cmd)
}

func (c *Client) start() {
	token := os.Getenv(tokenEnvVarKey)
	if token == "" {
		var err error
		if token, err = getToken(); err != nil {
			slog.Info("no token in keyring", "err", err)
			c.out.Emit("need_login", nil)
			return
		}
	}
	c.connect(token, false)
}

func (c *Client) logout() {
	c.mu.Lock()
	if c.cancelLogin != nil {
		c.cancelLogin()
		c.cancelLogin = nil
	}
	conn := c.conn
	c.conn = nil
	c.mu.Unlock()

	if conn != nil {
		conn.close()
	}
	deleteToken()
	c.out.Emit("need_login", nil)
}

func onRequest(r httpdriver.Request) error {
	if req, ok := r.(*httpdriver.DefaultRequest); ok {
		slog.Debug("new HTTP request", "method", req.Method, "url", req.URL)
	}
	return nil
}

// newState sets up a ningen state like discordo's chat model does.
func newState(token string) *ningen.State {
	id := gateway.NewIdentifier(gateway.IdentifyCommand{
		Token:      token,
		Properties: http.IdentifyProperties(),
		Capabilities: gateway.LazyUserNotes |
			gateway.VersionedReadStates |
			gateway.VersionedUserGuildSetttings |
			gateway.DedupeUserObjects |
			gateway.PrioritizedReadyPayload |
			gateway.MultipleGuildExperimentPopulations |
			gateway.NonChannelReadStates |
			gateway.AuthTokenRefresh |
			gateway.DebounceMessageReactions,
	})

	session := session.NewWithGateway(clientgateway.New(id), handler.New())
	session.Client = http.NewClient(token)
	st := ningen.FromState(state.NewFromSession(session, defaultstore.New()))
	st.StateLog = func(err error) {
		slog.Error("state log", "err", err)
	}
	st.OnRequest = append(st.OnRequest, httputil.WithHeaders(http.Headers()), onRequest)
	return st
}

// connect replaces the session with one for token.
func (c *Client) connect(token string, save bool) {
	conn := &conn{
		state:   newState(token),
		token:   token,
		save:    save,
		stop:    make(chan struct{}),
		members: make(map[discord.GuildID]int),
	}
	c.mu.Lock()
	threshold, input, output := c.threshold, c.input, c.output
	c.mu.Unlock()
	conn.voice = voice.New(conn.state.State, threshold)
	if input != nil || output != nil {
		if err := conn.voice.SetDevices(input, output); err != nil {
			slog.Warn("failed to set audio devices", "err", err)
		}
	}

	c.mu.Lock()
	old := c.conn
	c.conn = conn
	c.mu.Unlock()
	if old != nil {
		old.close()
	}

	q := newQueue()
	rm := conn.state.AddSyncHandler(func(ev gateway.Event) { q.push(ev) })
	c.wg.Go(func() {
		defer rm()
		c.listen(conn, q)
	})
	c.wg.Go(func() { c.watchVoice(conn) })
	c.wg.Go(func() { c.watchRelationships(conn) })

	c.out.Emit("connection", proto.F{"state": "connecting", "msg": ""})
	c.wg.Go(func() {
		err := conn.state.Open(context.Background())
		if err == nil {
			return
		}
		slog.Error("failed to open session", "err", err)

		select {
		case <-conn.stop:
			return
		default:
		}

		if event, ok := errors.AsType[*ws.CloseEvent](err); ok && event.Code == closeCodeAuthenticationFailed {
			c.mu.Lock()
			if c.conn == conn {
				c.conn = nil
			}
			c.mu.Unlock()
			conn.close()
			if !save {
				deleteToken()
			}
			c.out.Emit("login_error", proto.F{"msg": "the token was rejected, log in again"})
			c.out.Emit("need_login", nil)
			return
		}
		c.out.Emit("connection", proto.F{"state": "disconnected", "msg": err.Error()})
	})
}

func (conn *conn) close() {
	select {
	case <-conn.stop:
		return
	default:
	}
	close(conn.stop)

	if err := conn.voice.Leave(); err != nil {
		slog.Error("failed to leave voice", "err", err)
	}
	if err := conn.state.Close(); err != nil {
		slog.Error("failed to close the session", "err", err)
	}
}

func (conn *conn) memberCount(guildID discord.GuildID) int {
	conn.membersMu.Lock()
	defer conn.membersMu.Unlock()
	return conn.members[guildID]
}

func (conn *conn) setMemberCount(guildID discord.GuildID, n uint64) {
	if n == 0 {
		return
	}
	conn.membersMu.Lock()
	defer conn.membersMu.Unlock()
	conn.members[guildID] = int(n)
}

func (conn *conn) memberCounts() map[discord.GuildID]int {
	conn.membersMu.Lock()
	defer conn.membersMu.Unlock()
	m := make(map[discord.GuildID]int, len(conn.members))
	for k, v := range conn.members {
		m[k] = v
	}
	return m
}

// queue is an unbounded fifo of gateway events, so that a slow stdout never holds up the gateway.
type queue struct {
	mu     sync.Mutex
	events []gateway.Event
	signal chan struct{}
}

func newQueue() *queue {
	return &queue{signal: make(chan struct{}, 1)}
}

func (q *queue) push(ev gateway.Event) {
	q.mu.Lock()
	q.events = append(q.events, ev)
	q.mu.Unlock()
	select {
	case q.signal <- struct{}{}:
	default:
	}
}

func (q *queue) take() []gateway.Event {
	q.mu.Lock()
	defer q.mu.Unlock()
	events := q.events
	q.events = nil
	return events
}
