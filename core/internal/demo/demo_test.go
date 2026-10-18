package demo

import (
	"bufio"
	"bytes"
	json "encoding/json/v2"
	"io"
	"strings"
	"testing"
	"time"

	"ts3cord/core/internal/proto"
)

type event map[string]any

// run pipes cmds through the dispatcher into a demo with no live updates and returns the events.
func run(t *testing.T, cmds ...string) []event {
	t.Helper()
	var buf bytes.Buffer
	out := proto.NewEncoder(&buf)
	proto.Run(strings.NewReader(strings.Join(cmds, "\n")), out, New(out, Options{Seed: 1}))

	var evs []event
	for line := range strings.Lines(buf.String()) {
		var ev event
		if err := json.Unmarshal([]byte(line), &ev); err != nil {
			t.Fatalf("bad line %q: %v", line, err)
		}
		evs = append(evs, ev)
	}
	return evs
}

func find(evs []event, name string) []event {
	var out []event
	for _, ev := range evs {
		if ev["ev"] == name {
			out = append(out, ev)
		}
	}
	return out
}

func one(t *testing.T, evs []event, name string) event {
	t.Helper()
	found := find(evs, name)
	if len(found) != 1 {
		t.Fatalf("got %d %s events, want 1: %v", len(found), name, evs)
	}
	return found[0]
}

func list(v any) []map[string]any {
	var out []map[string]any
	for _, x := range v.([]any) {
		out = append(out, x.(map[string]any))
	}
	return out
}

const (
	cmdStart   = `{"cmd":"start"}`
	cmdGeneral = `{"cmd":"fetch_messages","channel_id":"400000000000000105"}`
)

func TestReady(t *testing.T) {
	ready := one(t, run(t, cmdStart), "ready")

	if me := ready["user"].(map[string]any); me["username"] != "leroy" {
		t.Errorf("user = %v", me)
	}
	if guilds := list(ready["guilds"]); len(guilds) != 3 {
		t.Errorf("got %d guilds, want 3", len(guilds))
	}
	dms := list(ready["dms"])
	if len(dms) < 2 {
		t.Fatalf("got %d dms", len(dms))
	}
	if len(dms[0]["recipients"].([]any)) == 0 {
		t.Error("dm without recipients")
	}
	if len(list(ready["friends"])) == 0 {
		t.Error("no friends")
	}
}

func TestNotLoggedIn(t *testing.T) {
	evs := run(t, `{"cmd":"open_guild","guild_id":"200000000000000001"}`)
	if e := one(t, evs, "error"); e["cmd"] != "open_guild" {
		t.Fatalf("error = %v", e)
	}
}

func TestGuildSnapshot(t *testing.T) {
	evs := run(t, cmdStart, `{"cmd":"open_guild","guild_id":"200000000000000001"}`)
	snap := one(t, evs, "guild_snapshot")

	var locked, noConnect, limited, cats int
	for _, ch := range list(snap["channels"]) {
		if ch["guild_id"] != "200000000000000001" {
			t.Errorf("channel %v in another guild", ch["id"])
		}
		switch {
		case ch["can_view"] == false:
			locked++
		case ch["can_connect"] == false:
			noConnect++
		}
		if ch["user_limit"].(float64) > 0 {
			limited++
		}
		if ch["type"].(float64) == 4 {
			cats++
		}
	}
	if locked == 0 || noConnect == 0 || limited == 0 || cats < 2 {
		t.Errorf("locked %d, no connect %d, limited %d, categories %d", locked, noConnect, limited, cats)
	}

	vss := list(snap["voice_states"])
	if len(vss) < 4 {
		t.Fatalf("got %d voice states", len(vss))
	}
	muted := 0
	for _, vs := range vss {
		if vs["guild_id"] != "200000000000000001" || vs["channel_id"] == "" {
			t.Errorf("voice state %v", vs)
		}
		if vs["self_mute"] == true {
			muted++
		}
	}
	if muted == 0 {
		t.Error("nobody muted")
	}
	if me := snap["me"].(map[string]any); me["nick"] != "LeroyJ" || len(me["roles"].([]any)) == 0 {
		t.Errorf("me = %v", me)
	}
	if len(list(snap["roles"])) < 3 {
		t.Error("too few roles")
	}
}

func TestFetchMessages(t *testing.T) {
	msgs := list(one(t, run(t, cmdStart, `{"cmd":"fetch_messages","channel_id":"400000000000000105","limit":100}`), "messages")["messages"])
	if len(msgs) < 38 || len(msgs) > 42 {
		t.Fatalf("got %d messages", len(msgs))
	}

	var attachment, reply, edited, mention, markdown, embed bool
	authors := map[any]bool{}
	for i, m := range msgs {
		if i > 0 && m["timestamp"].(string) < msgs[i-1]["timestamp"].(string) {
			t.Error("messages not oldest first")
		}
		authors[m["author"].(map[string]any)["id"]] = true
		attachment = attachment || len(m["attachments"].([]any)) > 0
		reply = reply || m["reply_to"] != nil
		edited = edited || m["edited_timestamp"] != ""
		mention = mention || (m["mentions_me"] == true && strings.Contains(m["content"].(string), "@LeroyJ"))
		markdown = markdown || strings.Contains(m["content"].(string), "**")
		embed = embed || len(m["embeds"].([]any)) > 0
	}
	if !attachment || !reply || !edited || !mention || !markdown || !embed {
		t.Errorf("attachment %v, reply %v, edited %v, mention %v, markdown %v, embed %v", attachment, reply, edited, mention, markdown, embed)
	}
	if len(authors) < 4 {
		t.Errorf("only %d authors", len(authors))
	}
}

func TestFetchPaging(t *testing.T) {
	evs := run(t, cmdStart, `{"cmd":"fetch_messages","channel_id":"400000000000000105","limit":10}`)
	page := one(t, evs, "messages")
	if page["has_more"] != true || len(list(page["messages"])) != 10 {
		t.Fatalf("first page = %d messages, has_more %v", len(list(page["messages"])), page["has_more"])
	}
}

func TestFetchLocked(t *testing.T) {
	evs := run(t, cmdStart, `{"cmd":"fetch_messages","channel_id":"400000000000000116"}`)
	if e := one(t, evs, "error"); e["cmd"] != "fetch_messages" {
		t.Fatalf("error = %v", e)
	}
}

// recorder keeps the events of a demo driven command by command.
type recorder struct {
	evs []event
}

func (r *recorder) Emit(ev string, fields proto.F) {
	data, _ := json.Marshal(fields)
	e := event{}
	_ = json.Unmarshal(data, &e)
	e["ev"] = ev
	r.evs = append(r.evs, e)
}

// do runs cmd on d and returns the events it made.
func (r *recorder) do(t *testing.T, d *Demo, cmd string) []event {
	t.Helper()
	c, err := proto.Decode([]byte(cmd))
	if err != nil {
		t.Fatal(err)
	}
	n := len(r.evs)
	if err := d.Handle(c); err != nil {
		t.Fatalf("%s: %v", cmd, err)
	}
	return r.evs[n:]
}

func TestSendEditDelete(t *testing.T) {
	r := &recorder{}
	d := New(r, Options{Seed: 1})
	defer d.Close()
	r.do(t, d, cmdStart)
	history := list(one(t, r.do(t, d, cmdGeneral), "messages")["messages"])

	created := one(t, r.do(t, d, `{"cmd":"send_message","channel_id":"400000000000000105","content":"hello **world**","nonce":"n-42","reply_to":"`+history[3]["id"].(string)+`"}`), "message_create")
	if created["nonce"] != "n-42" {
		t.Fatalf("nonce = %v", created["nonce"])
	}
	msg := created["message"].(map[string]any)
	if msg["content"] != "hello **world**" || msg["author"].(map[string]any)["username"] != "leroy" {
		t.Fatalf("message = %v", msg)
	}
	if reply, ok := msg["reply_to"].(map[string]any); !ok || reply["id"] != history[3]["id"] {
		t.Errorf("reply_to = %v", msg["reply_to"])
	}
	id := msg["id"].(string)
	if id <= history[len(history)-1]["id"].(string) && len(id) == len(history[0]["id"].(string)) {
		t.Errorf("id %s not after the history", id)
	}

	upd := one(t, r.do(t, d, `{"cmd":"edit_message","channel_id":"400000000000000105","message_id":"`+id+`","content":"edited"}`), "message_update")["message"].(map[string]any)
	if upd["content"] != "edited" || upd["edited_timestamp"] == "" || upd["id"] != id {
		t.Errorf("update = %v", upd)
	}
	if del := one(t, r.do(t, d, `{"cmd":"delete_message","channel_id":"400000000000000105","message_id":"`+id+`"}`), "message_delete"); del["message_id"] != id {
		t.Errorf("delete = %v", del)
	}

	// someone else's message cannot be edited.
	c, _ := proto.Decode([]byte(`{"cmd":"edit_message","channel_id":"400000000000000105","message_id":"` + history[0]["id"].(string) + `","content":"x"}`))
	if history[0]["author"].(map[string]any)["id"] != meID && d.Handle(c) == nil {
		t.Error("edited another user's message")
	}

	// the send shows up in the next fetch.
	again := list(one(t, r.do(t, d, cmdGeneral), "messages")["messages"])
	if again[len(again)-1]["id"] == id {
		t.Error("deleted message fetched")
	}
}

func TestVoice(t *testing.T) {
	evs := run(t, cmdStart,
		`{"cmd":"set_self_mute","mute":true}`,
		`{"cmd":"join_voice","guild_id":"200000000000000001","channel_id":"400000000000000110"}`,
		`{"cmd":"set_self_deaf","deaf":true}`,
		`{"cmd":"join_voice","guild_id":"200000000000000001","channel_id":"400000000000000117"}`,
		`{"cmd":"leave_voice"}`,
	)

	var states []string
	for _, ev := range find(evs, "voice_status") {
		states = append(states, ev["state"].(string))
	}
	if got := strings.Join(states, ","); got != "connecting,connected,disconnected,disconnected" {
		t.Fatalf("voice_status states = %s", got)
	}
	if find(evs, "voice_status")[2]["msg"] == "" {
		t.Error("joining a channel without connect gave no reason")
	}

	vss := find(evs, "voice_state")
	if len(vss) != 4 {
		t.Fatalf("got %d voice_state", len(vss))
	}
	joined := vss[1]["voice_state"].(map[string]any)
	if joined["channel_id"] != "400000000000000110" || joined["self_mute"] != true || joined["user"].(map[string]any)["id"] != meID {
		t.Errorf("joined = %v", joined)
	}
	if deaf := vss[2]["voice_state"].(map[string]any); deaf["self_deaf"] != true || deaf["self_mute"] != true {
		t.Errorf("deafened = %v", deaf)
	}
	if left := vss[3]["voice_state"].(map[string]any); left["channel_id"] != "" {
		t.Errorf("left = %v", left)
	}
}

func TestVoiceFull(t *testing.T) {
	evs := run(t, cmdStart, `{"cmd":"join_voice","guild_id":"200000000000000003","channel_id":"400000000000000306"}`)
	if st := one(t, evs, "voice_status"); st["state"] != "disconnected" || !strings.Contains(st["msg"].(string), "full") {
		t.Fatalf("voice_status = %v", st)
	}
}

func TestMisc(t *testing.T) {
	evs := run(t, cmdStart,
		`{"cmd":"set_status","status":"dnd"}`,
		`{"cmd":"user_profile","user_id":"300000000000000002","guild_id":"200000000000000001"}`,
		`{"cmd":"open_dm","user_id":"300000000000000013"}`,
		`{"cmd":"voice_settings","threshold":-50}`,
		`{"cmd":"ack","channel_id":"400000000000000105","message_id":"1"}`,
		`{"cmd":"typing","channel_id":"400000000000000105"}`,
		`{"cmd":"logout"}`,
	)
	if errs := find(evs, "error"); len(errs) > 0 {
		t.Fatalf("errors: %v", errs)
	}
	if p := find(evs, "presence"); p[len(p)-1]["status"] != "dnd" {
		t.Errorf("presence = %v", p[len(p)-1])
	}
	prof := one(t, evs, "user_profile")
	if prof["user"].(map[string]any)["display"] != "[QC] Dave" || len(list(prof["roles"])) == 0 || prof["joined_at"] == "" {
		t.Errorf("profile = %v", prof)
	}
	dm := one(t, evs, "dm_open")["channel"].(map[string]any)
	if dm["type"].(float64) != 1 || list(dm["recipients"])[0]["id"] != "300000000000000013" {
		t.Errorf("dm = %v", dm)
	}
	if devs := one(t, evs, "voice_devices"); len(devs["inputs"].([]any)) == 0 {
		t.Errorf("devices = %v", devs)
	}
	if u := one(t, evs, "unread"); u["unread"] != false {
		t.Errorf("unread = %v", u)
	}
	one(t, evs, "need_login")
}

func TestLogins(t *testing.T) {
	evs := run(t, `{"cmd":"login_password","login":"a@b.c","password":"mfa"}`, `{"cmd":"login_mfa","ticket":"demo-ticket","code":"123456","method":"totp"}`)
	if mfa := one(t, evs, "mfa_required"); mfa["ticket"] != "demo-ticket" {
		t.Errorf("mfa = %v", mfa)
	}
	one(t, evs, "ready")

	evs = run(t, `{"cmd":"qr_start"}`)
	one(t, evs, "qr")
	one(t, evs, "qr_user")
	one(t, evs, "ready")

	evs = run(t, `{"cmd":"login_password","login":"a@b.c","password":""}`)
	one(t, evs, "login_error")
}

// liveEvents collects the events of a live demo.
func liveEvents(r io.Reader) <-chan event {
	ch := make(chan event, 1024)
	go func() {
		defer close(ch)
		scanner := bufio.NewScanner(r)
		scanner.Buffer(nil, 1<<24)
		for scanner.Scan() {
			var ev event
			if json.Unmarshal(scanner.Bytes(), &ev) == nil {
				ch <- ev
			}
		}
	}()
	return ch
}

func TestLive(t *testing.T) {
	inR, inW := io.Pipe()
	outR, outW := io.Pipe()
	out := proto.NewEncoder(outW)
	d := New(out, Options{Seed: 7, SpeakEvery: 5 * time.Millisecond, ChatEvery: 20 * time.Millisecond, ChurnEvery: 15 * time.Millisecond, TypingFor: 5 * time.Millisecond})
	done := make(chan struct{})
	go func() {
		proto.Run(inR, out, d)
		outW.Close()
		close(done)
	}()
	evs := liveEvents(outR)

	for _, cmd := range []string{cmdStart, cmdGeneral, `{"cmd":"join_voice","guild_id":"200000000000000001","channel_id":"400000000000000110"}`} {
		if _, err := io.WriteString(inW, cmd+"\n"); err != nil {
			t.Fatal(err)
		}
	}

	want := map[string]bool{"speaking": false, "typing": false, "message_create": false, "voice_state": false}
	timeout := time.After(10 * time.Second)
	for remaining := len(want); remaining > 0; {
		select {
		case ev, ok := <-evs:
			if !ok {
				t.Fatal("output ended")
			}
			name, _ := ev["ev"].(string)
			if seen, ok := want[name]; ok && !seen {
				want[name] = true
				remaining--
			}
			if name == "speaking" && ev["guild_id"] != "200000000000000001" {
				t.Errorf("speaking in %v", ev["guild_id"])
			}
		case <-timeout:
			t.Fatalf("timed out, seen %v", want)
		}
	}

	// closing stdin ends the demo cleanly.
	inW.Close()
	go func() {
		for range evs {
		}
	}()
	select {
	case <-done:
	case <-time.After(5 * time.Second):
		t.Fatal("did not exit after stdin closed")
	}
}
