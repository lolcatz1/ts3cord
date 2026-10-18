// Package demo answers the protocol with a fake world and no network, for working on the ui.
package demo

import (
	"cmp"
	"errors"
	"image"
	_ "image/gif"
	_ "image/jpeg"
	_ "image/png"
	"math/rand/v2"
	"mime"
	"net/url"
	"os"
	"path/filepath"
	"slices"
	"strconv"
	"strings"
	"sync"
	"time"

	"ts3cord/core/internal/proto"
)

const discordEpoch = 1420070400000

type Options struct {
	Seed uint64
	// speakevery, chatevery and churnevery are how often the fake users speak, chat and move around. 0 disables one.
	SpeakEvery time.Duration
	ChatEvery  time.Duration
	ChurnEvery time.Duration
	// typingFor is how long a fake user types before the message arrives.
	TypingFor time.Duration
	// dms lets live chat land in dms too.
	DMs bool
}

func DefaultOptions() Options {
	return Options{
		Seed:       uint64(time.Now().UnixNano()),
		SpeakEvery: 700 * time.Millisecond,
		ChatEvery:  15 * time.Second,
		ChurnEvery: 25 * time.Second,
		DMs:        true,
		TypingFor:  3 * time.Second,
	}
}

type Demo struct {
	out  proto.Emitter
	opts Options

	mu       sync.Mutex
	rng      *rand.Rand
	seq      uint64
	loggedIn bool
	guilds   []*fakeGuild
	users    map[string]fakeUser
	dms      []fakeDM
	voice    map[string]*fakeVoice
	speaking map[string]bool
	messages map[string][]proto.Message
	status   string
	// selfmute and selfdeaf are leroy's voice flags, kept out of voice too.
	selfMute, selfDeaf bool
	// current is the channel last fetched, where live messages mostly go.
	current string

	stop chan struct{}
	wg   sync.WaitGroup
}

func New(out proto.Emitter, opts Options) *Demo {
	d := &Demo{
		out:      out,
		opts:     opts,
		rng:      rand.New(rand.NewPCG(opts.Seed, opts.Seed^0x9e3779b97f4a7c15)),
		guilds:   guilds(),
		users:    make(map[string]fakeUser),
		dms:      slices.Clone(dms),
		voice:    make(map[string]*fakeVoice),
		speaking: make(map[string]bool),
		messages: make(map[string][]proto.Message),
		status:   "online",
		stop:     make(chan struct{}),
	}
	for _, u := range users {
		d.users[u.id] = u
	}
	for _, v := range voiceStates() {
		d.voice[v.user] = &v
	}

	d.loop(opts.SpeakEvery, d.speakTick)
	d.loop(opts.ChatEvery, d.chatTick)
	d.loop(opts.ChurnEvery, d.churnTick)
	return d
}

func (d *Demo) loop(every time.Duration, tick func()) {
	if every <= 0 {
		return
	}
	d.wg.Go(func() {
		t := time.NewTicker(every)
		defer t.Stop()
		for {
			select {
			case <-d.stop:
				return
			case <-t.C:
				tick()
			}
		}
	})
}

func (d *Demo) Close() {
	close(d.stop)
	d.wg.Wait()
}

func (d *Demo) Handle(cmd proto.Command) error {
	d.mu.Lock()
	defer d.mu.Unlock()

	switch cmd.Cmd {
	case "start", "login_token":
		d.ready()
	case "login_password":
		// password "mfa" asks for a code, for testing that screen.
		if cmd.Password == "mfa" {
			d.out.Emit("mfa_required", proto.F{"ticket": "demo-ticket", "methods": []string{"totp"}})
			return nil
		}
		if cmd.Password == "" {
			d.out.Emit("login_error", proto.F{"msg": "Login or password is invalid."})
			return nil
		}
		d.ready()
	case "login_mfa":
		if len(cmd.Code) != 6 {
			d.out.Emit("login_error", proto.F{"msg": "Invalid two-factor code"})
			return nil
		}
		d.ready()
	case "qr_start":
		d.out.Emit("qr", proto.F{"url": "https://discord.com/ra/demo-fingerprint"})
		d.out.Emit("qr_user", proto.F{"username": "leroy", "avatar_url": avatarURL(0)})
		d.ready()
	case "logout":
		d.loggedIn = false
		d.leaveVoice()
		d.out.Emit("need_login", nil)
	default:
		if !d.loggedIn {
			return errors.New("not logged in")
		}
		return d.handleChat(cmd)
	}
	return nil
}

func (d *Demo) handleChat(cmd proto.Command) error {
	switch cmd.Cmd {
	case "open_guild":
		g := d.guild(cmd.GuildID)
		if g == nil {
			return errors.New("unknown guild")
		}
		d.out.Emit("guild_snapshot", d.snapshot(g))
	case "fetch_messages":
		return d.fetch(cmd)
	case "send_message":
		ch, _ := d.channel(cmd.ChannelID)
		if ch == nil {
			return errors.New("unknown channel")
		}
		if !ch.CanSend {
			return errors.New("missing permissions")
		}
		msg := d.newMessage(ch, meID, cmd.Content, time.Now())
		for _, path := range cmd.Files {
			msg.Attachments = append(msg.Attachments, localAttachment(path))
		}
		if cmd.ReplyTo != "" {
			if ref := d.findMessage(ch.ID, cmd.ReplyTo); ref != nil {
				msg.ReplyTo = &proto.Reply{ID: ref.ID, Author: ref.Author, Content: ref.Content}
				msg.Type = 19
			}
		}
		d.messages[ch.ID] = append(d.history(ch), msg)
		d.out.Emit("message_create", proto.F{"message": msg, "nonce": cmd.Nonce})
	case "edit_message":
		msg := d.findMessage(cmd.ChannelID, cmd.MessageID)
		if msg == nil {
			return errors.New("unknown message")
		}
		if msg.Author.ID != meID {
			return errors.New("cannot edit a message authored by another user")
		}
		msg.Content = cmd.Content
		msg.EditedTimestamp = time.Now().UTC().Format(time.RFC3339)
		d.out.Emit("message_update", proto.F{"message": *msg})
	case "delete_message":
		msgs := d.messages[cmd.ChannelID]
		i := slices.IndexFunc(msgs, func(m proto.Message) bool { return m.ID == cmd.MessageID })
		if i < 0 {
			return errors.New("unknown message")
		}
		d.messages[cmd.ChannelID] = slices.Delete(msgs, i, i+1)
		d.out.Emit("message_delete", proto.F{"channel_id": cmd.ChannelID, "message_id": cmd.MessageID})
	case "typing":
	case "ack":
		d.out.Emit("unread", proto.F{"channel_id": cmd.ChannelID, "unread": false, "mentions": 0})
	case "join_voice":
		d.joinVoice(cmd.GuildID, cmd.ChannelID)
	case "leave_voice":
		d.leaveVoice()
	case "set_self_mute", "set_self_deaf":
		if cmd.Cmd == "set_self_mute" {
			d.selfMute = cmd.Mute
			if !cmd.Mute {
				d.selfDeaf = false
			}
		} else {
			d.selfDeaf = cmd.Deaf
			if cmd.Deaf {
				d.selfMute = true
			}
		}
		v := d.voice[meID]
		if v == nil {
			// not in voice, the flags apply on the next join.
			d.out.Emit("voice_state", proto.F{"voice_state": proto.VoiceState{User: d.user(meID, ""), SelfMute: d.selfMute, SelfDeaf: d.selfDeaf}})
			return nil
		}
		v.selfMute, v.selfDeaf = d.selfMute, d.selfDeaf
		if v.selfMute && d.speaking[meID] {
			d.setSpeaking(v.guild, meID, false)
		}
		d.out.Emit("voice_state", proto.F{"voice_state": d.voiceState(v)})
	case "set_status":
		switch cmd.Status {
		case "online", "idle", "dnd", "invisible":
		default:
			return errors.New("unknown status")
		}
		d.status = cmd.Status
		d.out.Emit("presence", proto.F{"user_id": meID, "status": cmd.Status, "activity": ""})
	case "user_profile":
		return d.profile(cmd.UserID, cmd.GuildID)
	case "open_dm":
		u, ok := d.users[cmd.UserID]
		if !ok || cmd.UserID == meID {
			return errors.New("unknown user")
		}
		i := slices.IndexFunc(d.dms, func(dm fakeDM) bool { return !dm.group && dm.recipients[0] == u.id })
		if i < 0 {
			d.dms = append(d.dms, fakeDM{id: d.snowflake(time.Now()), recipients: []string{u.id}})
			i = len(d.dms) - 1
		}
		d.out.Emit("dm_open", proto.F{"channel": d.dmChannel(d.dms[i])})
	case "voice_settings":
		d.out.Emit("voice_devices", proto.F{"inputs": fakeInputs, "outputs": fakeOutputs})
	}
	return nil
}

func (d *Demo) ready() {
	d.loggedIn = true
	d.out.Emit("connection", proto.F{"state": "connected", "msg": ""})

	gs := make([]proto.Guild, 0, len(d.guilds))
	for _, g := range d.guilds {
		gs = append(gs, proto.Guild{ID: g.info.ID, Name: g.info.Name, IconURL: g.info.IconURL, OwnerID: g.info.OwnerID, MemberCount: g.info.MemberCount})
	}
	chs := make([]proto.Channel, 0, len(d.dms))
	for _, dm := range d.dms {
		chs = append(chs, d.dmChannel(dm))
	}
	fs := make([]proto.Friend, 0, len(friends))
	for _, f := range friends {
		fs = append(fs, proto.Friend{User: d.user(f.user, ""), Type: f.typ, Ignored: f.ignored})
	}
	d.out.Emit("ready", proto.F{"user": d.user(meID, ""), "guilds": gs, "dms": chs, "friends": fs})
	for _, u := range users {
		if u.status != "offline" {
			d.out.Emit("presence", proto.F{"user_id": u.id, "status": u.status, "activity": u.activity})
		}
	}
}

func (d *Demo) snapshot(g *fakeGuild) proto.F {
	var vs []proto.VoiceState
	for _, v := range d.sortedVoice() {
		if v.guild == g.info.ID {
			vs = append(vs, d.voiceState(v))
		}
	}
	me := g.members[meID]
	if me.roles == nil {
		me.roles = []string{}
	}
	chs := slices.Clone(g.channels)
	for i := range chs {
		chs[i].LastMessageID = d.lastMessageID(&chs[i])
	}
	return proto.F{
		"guild":        g.info,
		"channels":     chs,
		"voice_states": vs,
		"roles":        g.roles,
		"me":           proto.Me{Nick: me.nick, Roles: me.roles},
	}
}

func (d *Demo) sortedVoice() []*fakeVoice {
	vs := make([]*fakeVoice, 0, len(d.voice))
	for _, v := range d.voice {
		vs = append(vs, v)
	}
	slices.SortFunc(vs, func(a, b *fakeVoice) int { return strings.Compare(a.user, b.user) })
	return vs
}

func (d *Demo) lastMessageID(ch *proto.Channel) string {
	if msgs := d.messages[ch.ID]; len(msgs) > 0 {
		return msgs[len(msgs)-1].ID
	}
	return ""
}

func (d *Demo) guild(id string) *fakeGuild {
	for _, g := range d.guilds {
		if g.info.ID == id {
			return g
		}
	}
	return nil
}

// channel returns the guild or dm channel id and its guild, nil for a dm.
func (d *Demo) channel(id string) (*proto.Channel, *fakeGuild) {
	for _, g := range d.guilds {
		for i := range g.channels {
			if g.channels[i].ID == id {
				return &g.channels[i], g
			}
		}
	}
	for _, dm := range d.dms {
		if dm.id == id {
			ch := d.dmChannel(dm)
			return &ch, nil
		}
	}
	return nil, nil
}

func (d *Demo) dmChannel(dm fakeDM) proto.Channel {
	ch := proto.Channel{ID: dm.id, Type: 1, Name: dm.name, CanView: true, CanSend: true, CanConnect: true, Muted: dm.muted}
	if dm.group {
		ch.Type = 3
	}
	for _, r := range dm.recipients {
		ch.Recipients = append(ch.Recipients, d.user(r, ""))
	}
	if msgs := d.messages[dm.id]; len(msgs) > 0 {
		ch.LastMessageID = msgs[len(msgs)-1].ID
	}
	return ch
}

func avatarURL(n int) string {
	return "https://cdn.discordapp.com/embed/avatars/" + strconv.Itoa(n) + ".png"
}

// user returns the user id as shown in the guild id, or outside of guilds if "".
func (d *Demo) user(id, guildID string) proto.User {
	u, ok := d.users[id]
	if !ok {
		return proto.User{ID: id, Username: id, Display: id, AvatarURL: avatarURL(0)}
	}
	display := cmp.Or(u.global, u.username)
	if g := d.guild(guildID); g != nil {
		display = cmp.Or(g.members[id].nick, display)
	}
	return proto.User{ID: u.id, Username: u.username, GlobalName: u.global, Display: display, AvatarURL: avatarURL(u.avatar), Bot: u.bot}
}

func (d *Demo) color(id, guildID string) string {
	g := d.guild(guildID)
	if g == nil {
		return ""
	}
	color, pos := "", -1
	for _, r := range g.roles {
		if r.Color != "" && r.Position > pos && slices.Contains(g.members[id].roles, r.ID) {
			color, pos = r.Color, r.Position
		}
	}
	return color
}

func (d *Demo) voiceState(v *fakeVoice) proto.VoiceState {
	return proto.VoiceState{
		GuildID:   v.guild,
		ChannelID: v.channel,
		User:      d.user(cmp.Or(v.user, meID), v.guild),
		SelfMute:  v.selfMute,
		SelfDeaf:  v.selfDeaf,
		Mute:      v.mute,
	}
}

func (d *Demo) snowflake(t time.Time) string {
	d.seq++
	return strconv.FormatUint(uint64(t.UnixMilli()-discordEpoch)<<22|d.seq&0x3fffff, 10)
}

// members returns who can talk in the guild of ch, or the dm, without leroy.
func (d *Demo) members(ch *proto.Channel, g *fakeGuild) []string {
	var ids []string
	if g == nil {
		for _, dm := range d.dms {
			if dm.id == ch.ID {
				ids = slices.Clone(dm.recipients)
			}
		}
		return ids
	}
	for id := range g.members {
		if id != meID {
			ids = append(ids, id)
		}
	}
	slices.Sort(ids)
	return ids
}

func (d *Demo) newMessage(ch *proto.Channel, author, content string, at time.Time) proto.Message {
	return proto.Message{
		ID:          d.snowflake(at),
		ChannelID:   ch.ID,
		GuildID:     ch.GuildID,
		Author:      d.user(author, ch.GuildID),
		AuthorColor: d.color(author, ch.GuildID),
		Content:     content,
		Timestamp:   at.UTC().Format(time.RFC3339),
		MentionsMe:  author != meID && strings.Contains(content, "@"+d.user(meID, ch.GuildID).Display),
		Attachments: []proto.Attachment{},
		Embeds:      []proto.Embed{},
	}
}

// fill replaces {me} and {u} in a line.
func (d *Demo) fill(line string, ch *proto.Channel, others []string) string {
	line = strings.ReplaceAll(line, "{me}", "@"+d.user(meID, ch.GuildID).Display)
	if strings.Contains(line, "{u}") && len(others) > 0 {
		line = strings.ReplaceAll(line, "{u}", "@"+d.user(others[d.rng.IntN(len(others))], ch.GuildID).Display)
	}
	return line
}

// history returns the messages of ch, making up about 40 the first time.
func (d *Demo) history(ch *proto.Channel) []proto.Message {
	if msgs, ok := d.messages[ch.ID]; ok {
		return msgs
	}

	authors := d.members(ch, d.guildOf(ch))
	pool := lines
	if ch.Type == 1 || ch.Type == 3 {
		pool = dmLines
	}
	n := 38 + d.rng.IntN(5)
	at := time.Now().Add(-time.Duration(n) * 9 * time.Minute)
	msgs := make([]proto.Message, 0, n)
	for i := range n {
		at = at.Add(time.Duration(2+d.rng.IntN(14)) * time.Minute)
		author := authors[d.rng.IntN(len(authors))]
		if i%7 == 3 {
			author = meID
		}
		content := d.fill(pool[d.rng.IntN(len(pool))], ch, authors)
		msg := d.newMessage(ch, author, content, at)
		switch i {
		case 6:
			msg.Content = "check this out"
			msg.Attachments = []proto.Attachment{{Filename: "screenshot_q3dm17.png", URL: "https://cdn.discordapp.com/attachments/" + ch.ID + "/" + msg.ID + "/screenshot_q3dm17.png", Size: 482113, ContentType: "image/png", Width: 1024, Height: 768}}
		case 11:
			msg.Content = "https://github.com/ayn2op/discordo"
			msg.Embeds = []proto.Embed{{Title: "ayn2op/discordo", Description: "A lightweight, secure, and feature-rich Discord terminal client.", URL: "https://github.com/ayn2op/discordo", Author: "GitHub", Footer: "github.com"}}
		case 15:
			ref := msgs[13]
			msg.Type = 19
			msg.ReplyTo = &proto.Reply{ID: ref.ID, Author: ref.Author, Content: ref.Content}
			msg.Content = "^ this, 100%"
		case 22:
			msg.EditedTimestamp = at.Add(90 * time.Second).UTC().Format(time.RFC3339)
			msg.Content = "fixed the typo, *definitely* not a ninja edit"
		case 27:
			msg.Content = "**bold**, *italic*, `code`, ~~gone~~ and a [link](https://example.com)\n```go\nfmt.Println(\"hello from ts3cord\")\n```"
		case n - 4:
			author = authors[0]
			msg = d.newMessage(ch, author, "@"+d.user(meID, ch.GuildID).Display+" are you joining voice later?", at)
		}
		msgs = append(msgs, msg)
	}
	d.messages[ch.ID] = msgs
	return msgs
}

func (d *Demo) guildOf(ch *proto.Channel) *fakeGuild {
	return d.guild(ch.GuildID)
}

func (d *Demo) findMessage(channelID, id string) *proto.Message {
	msgs := d.messages[channelID]
	for i := range msgs {
		if msgs[i].ID == id {
			return &msgs[i]
		}
	}
	return nil
}

func (d *Demo) fetch(cmd proto.Command) error {
	ch, _ := d.channel(cmd.ChannelID)
	if ch == nil {
		return errors.New("unknown channel")
	}
	if !ch.CanView || ch.Type == 2 || ch.Type == 4 {
		return errors.New("missing access")
	}
	d.current = ch.ID

	msgs := d.history(ch)
	if cmd.Before != "" {
		i := slices.IndexFunc(msgs, func(m proto.Message) bool { return m.ID == cmd.Before })
		if i < 0 {
			i = 0
		}
		msgs = msgs[:i]
	}
	limit := cmd.Limit
	if limit <= 0 || limit > 100 {
		limit = 50
	}
	hasMore := len(msgs) > limit
	if hasMore {
		msgs = msgs[len(msgs)-limit:]
	}
	d.out.Emit("messages", proto.F{"channel_id": ch.ID, "messages": slices.Clone(msgs), "has_more": hasMore})
	return nil
}

func (d *Demo) joinVoice(guildID, channelID string) {
	ch, g := d.channel(channelID)
	fail := func(msg string) {
		d.out.Emit("voice_status", proto.F{"state": "disconnected", "guild_id": guildID, "channel_id": channelID, "ping_ms": 0, "msg": msg})
	}
	switch {
	case ch == nil || g == nil || ch.Type != 2:
		fail("unknown voice channel")
		return
	case !ch.CanConnect || !ch.CanView:
		fail("no permission to connect")
		return
	case ch.UserLimit > 0 && d.count(ch.ID) >= ch.UserLimit:
		fail("channel is full")
		return
	}

	d.out.Emit("voice_status", proto.F{"state": "connecting", "guild_id": g.info.ID, "channel_id": ch.ID, "ping_ms": 0, "msg": ""})
	v := d.voice[meID]
	if v == nil {
		v = &fakeVoice{user: meID, selfMute: d.selfMute, selfDeaf: d.selfDeaf}
		d.voice[meID] = v
	} else if v.guild != g.info.ID {
		// leaving the other guild's channel first, like discord does.
		d.out.Emit("voice_state", proto.F{"voice_state": proto.VoiceState{GuildID: v.guild, User: d.user(meID, v.guild), SelfMute: v.selfMute, SelfDeaf: v.selfDeaf}})
	}
	clear(d.speaking)
	v.guild, v.channel = g.info.ID, ch.ID
	d.out.Emit("voice_state", proto.F{"voice_state": d.voiceState(v)})
	d.out.Emit("voice_status", proto.F{"state": "connected", "guild_id": g.info.ID, "channel_id": ch.ID, "ping_ms": 30 + d.rng.IntN(25), "msg": ""})
}

func (d *Demo) leaveVoice() {
	v := d.voice[meID]
	if v == nil {
		return
	}
	delete(d.voice, meID)
	clear(d.speaking)
	d.out.Emit("voice_state", proto.F{"voice_state": proto.VoiceState{GuildID: v.guild, User: d.user(meID, v.guild), SelfMute: v.selfMute, SelfDeaf: v.selfDeaf}})
	d.out.Emit("voice_status", proto.F{"state": "disconnected", "guild_id": v.guild, "channel_id": "", "ping_ms": 0, "msg": ""})
}

func (d *Demo) count(channelID string) int {
	n := 0
	for _, v := range d.voice {
		if v.channel == channelID {
			n++
		}
	}
	return n
}

func (d *Demo) setSpeaking(guildID, userID string, speaking bool) {
	if d.speaking[userID] == speaking {
		return
	}
	d.speaking[userID] = speaking
	d.out.Emit("speaking", proto.F{"guild_id": guildID, "user_id": userID, "speaking": speaking})
}

// speakTick toggles a random user who can talk in leroy's voice channel.
func (d *Demo) speakTick() {
	d.mu.Lock()
	defer d.mu.Unlock()

	me := d.voice[meID]
	if !d.loggedIn || me == nil {
		return
	}
	var talkers []string
	for _, v := range d.sortedVoice() {
		if v.channel == me.channel && !v.selfMute && !v.mute && (v.user != meID || d.rng.IntN(4) == 0) {
			talkers = append(talkers, v.user)
		}
	}
	if len(talkers) == 0 {
		return
	}
	id := talkers[d.rng.IntN(len(talkers))]
	d.setSpeaking(me.guild, id, !d.speaking[id])
	if me.channel != "" && d.rng.IntN(5) == 0 {
		d.out.Emit("voice_status", proto.F{"state": "connected", "guild_id": me.guild, "channel_id": me.channel, "ping_ms": 30 + d.rng.IntN(25), "msg": ""})
	}
}

// chatTick makes someone type and then say something, mostly in the channel last fetched.
func (d *Demo) chatTick() {
	d.mu.Lock()
	if !d.loggedIn {
		d.mu.Unlock()
		return
	}
	id := d.current
	if id == "" || d.rng.IntN(4) == 0 {
		g := d.guilds[d.rng.IntN(len(d.guilds))]
		var texts []string
		for _, c := range g.channels {
			if c.Type == 0 && c.CanView {
				texts = append(texts, c.ID)
			}
		}
		id = texts[d.rng.IntN(len(texts))]
	}
	// now and then a dm instead, from anyone incl. ignored users and muted chats
	if d.opts.DMs && d.rng.IntN(3) == 0 {
		id = d.dms[d.rng.IntN(len(d.dms))].id
	}
	ch, g := d.channel(id)
	authors := d.members(ch, g)
	if g == nil {
		authors = nil
		for _, r := range ch.Recipients {
			authors = append(authors, r.ID)
		}
	}
	if len(authors) == 0 {
		d.mu.Unlock()
		return
	}
	author := authors[d.rng.IntN(len(authors))]
	line := d.fill(liveLines[d.rng.IntN(len(liveLines))], ch, authors)
	chCopy := *ch
	d.out.Emit("typing", proto.F{"channel_id": ch.ID, "user": d.user(author, ch.GuildID)})
	d.mu.Unlock()

	select {
	case <-d.stop:
		return
	case <-time.After(d.opts.TypingFor):
	}

	d.mu.Lock()
	defer d.mu.Unlock()
	if !d.loggedIn {
		return
	}
	msg := d.newMessage(&chCopy, author, line, time.Now())
	d.messages[chCopy.ID] = append(d.history(&chCopy), msg)
	d.out.Emit("message_create", proto.F{"message": msg, "nonce": ""})
	if chCopy.ID != d.current {
		mentions := 0
		if msg.MentionsMe {
			mentions = 1
		}
		d.out.Emit("unread", proto.F{"channel_id": chCopy.ID, "unread": true, "mentions": mentions})
	}
}

// churnTick moves a fake user to another voice channel or out of voice, or changes a status.
func (d *Demo) churnTick() {
	d.mu.Lock()
	defer d.mu.Unlock()

	if !d.loggedIn {
		return
	}

	if d.rng.IntN(3) == 0 {
		u := users[1+d.rng.IntN(len(users)-1)]
		statuses := []string{"online", "idle", "dnd", "offline"}
		d.out.Emit("presence", proto.F{"user_id": u.id, "status": statuses[d.rng.IntN(len(statuses))], "activity": u.activity})
		return
	}

	g := d.guilds[d.rng.IntN(len(d.guilds))]
	ids := d.members(&proto.Channel{}, g)
	id := ids[d.rng.IntN(len(ids))]
	v := d.voice[id]
	if v != nil && v.guild != g.info.ID {
		return
	}

	var targets []string
	for _, c := range g.channels {
		if c.Type == 2 && c.CanView && c.CanConnect && (c.UserLimit == 0 || d.count(c.ID) < c.UserLimit) {
			targets = append(targets, c.ID)
		}
	}

	if v != nil && (d.rng.IntN(2) == 0 || len(targets) == 0) {
		delete(d.voice, id)
		if d.speaking[id] {
			d.setSpeaking(g.info.ID, id, false)
		}
		d.out.Emit("voice_state", proto.F{"voice_state": proto.VoiceState{GuildID: g.info.ID, User: d.user(id, g.info.ID)}})
		return
	}
	if len(targets) == 0 {
		return
	}
	if v == nil {
		v = &fakeVoice{guild: g.info.ID, user: id, selfMute: d.rng.IntN(4) == 0}
		d.voice[id] = v
	}
	if d.speaking[id] {
		d.setSpeaking(g.info.ID, id, false)
	}
	v.channel = targets[d.rng.IntN(len(targets))]
	d.out.Emit("voice_state", proto.F{"voice_state": d.voiceState(v)})
}

func (d *Demo) profile(userID, guildID string) error {
	u, ok := d.users[userID]
	if !ok {
		return errors.New("unknown user")
	}
	roles := []proto.ProfileRole{}
	joined := ""
	if g := d.guild(guildID); g != nil {
		m, ok := g.members[userID]
		if ok {
			joined = m.joined
		}
		for _, r := range slices.Backward(g.roles) {
			if slices.Contains(m.roles, r.ID) {
				roles = append(roles, proto.ProfileRole{Name: r.Name, Color: r.Color})
			}
		}
	}
	status, activity := u.status, u.activity
	if userID == meID {
		status = d.status
	}
	d.out.Emit("user_profile", proto.F{
		"user":          d.user(userID, guildID),
		"bio":           u.bio,
		"created_at":    u.created,
		"joined_at":     joined,
		"roles":         roles,
		"status":        status,
		"activity":      activity,
		"premium_since": "",
	})
	return nil
}

// localAttachment describes a file on disk the way discord would after the upload.
func localAttachment(path string) proto.Attachment {
	a := proto.Attachment{Filename: filepath.Base(path), URL: (&url.URL{Scheme: "file", Path: path}).String(), ContentType: mime.TypeByExtension(filepath.Ext(path))}
	if st, err := os.Stat(path); err == nil {
		a.Size = int(st.Size())
	}
	if f, err := os.Open(path); err == nil {
		if cfg, _, err := image.DecodeConfig(f); err == nil {
			a.Width, a.Height = cfg.Width, cfg.Height
		}
		f.Close()
	}
	return a
}
