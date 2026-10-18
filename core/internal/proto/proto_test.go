package proto

import (
	"bufio"
	"bytes"
	"encoding/json/jsontext"
	json "encoding/json/v2"
	"errors"
	"strings"
	"sync"
	"testing"
)

type recorder struct {
	cmds   []Command
	closed bool
	fail   error
}

func (r *recorder) Handle(cmd Command) error {
	r.cmds = append(r.cmds, cmd)
	return r.fail
}

func (r *recorder) Close() { r.closed = true }

func events(t *testing.T, out string) []map[string]any {
	t.Helper()
	var evs []map[string]any
	for line := range strings.Lines(out) {
		var ev map[string]any
		if err := json.Unmarshal([]byte(line), &ev); err != nil {
			t.Fatalf("bad line %q: %v", line, err)
		}
		evs = append(evs, ev)
	}
	return evs
}

func TestEncoderLine(t *testing.T) {
	var buf bytes.Buffer
	e := NewEncoder(&buf)
	e.Emit("message_delete", F{"channel_id": "1", "message_id": "2"})
	e.Emit("need_login", nil)

	want := `{"channel_id":"1","ev":"message_delete","message_id":"2"}` + "\n" + `{"ev":"need_login"}` + "\n"
	if got := buf.String(); got != want {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestEncoderObjects(t *testing.T) {
	var buf bytes.Buffer
	NewEncoder(&buf).Emit("messages", F{"channel_id": "1", "messages": []Message{{ID: "5"}}, "has_more": false})

	evs := events(t, buf.String())
	msg := evs[0]["messages"].([]any)[0].(map[string]any)
	// nil slices are [] and no reply is null, as the ui expects.
	if _, ok := msg["attachments"].([]any); !ok {
		t.Errorf("attachments = %v, want []", msg["attachments"])
	}
	if v, ok := msg["reply_to"]; !ok || v != nil {
		t.Errorf("reply_to = %v, want null", v)
	}
	if msg["edited_timestamp"] != "" {
		t.Errorf("edited_timestamp = %v, want empty", msg["edited_timestamp"])
	}
	ch, _ := json.Marshal(Channel{ID: "1"})
	if bytes.Contains(ch, []byte("recipients")) {
		t.Errorf("guild channel has recipients: %s", ch)
	}
}

func TestEncoderConcurrent(t *testing.T) {
	var buf bytes.Buffer
	e := NewEncoder(&buf)
	var wg sync.WaitGroup
	for i := range 50 {
		wg.Go(func() { e.Emit("typing", F{"channel_id": strings.Repeat("x", i*100)}) })
	}
	wg.Wait()

	scanner := bufio.NewScanner(&buf)
	scanner.Buffer(nil, 1<<20)
	n := 0
	for scanner.Scan() {
		if !jsontext.Value(scanner.Bytes()).IsValid() {
			t.Fatalf("interleaved line %q", scanner.Text())
		}
		n++
	}
	if n != 50 {
		t.Fatalf("got %d lines, want 50", n)
	}
}

func TestDecode(t *testing.T) {
	cmd, err := Decode([]byte(`{"cmd":"voice_settings","threshold":-50.5,"input_device":"mic","limit":3,"mute":true,"extra":1}`))
	if err != nil {
		t.Fatal(err)
	}
	if cmd.Cmd != "voice_settings" || cmd.Threshold == nil || *cmd.Threshold != -50.5 || cmd.InputDevice == nil || *cmd.InputDevice != "mic" || cmd.OutputDevice != nil || cmd.Limit != 3 || !cmd.Mute {
		t.Fatalf("got %+v", cmd)
	}
}

func TestRun(t *testing.T) {
	in := strings.Join([]string{
		`{"cmd":"start"}`,
		``,
		`{"cmd":"nope"}`,
		`{oops`,
		`{"cmd":"send_message","channel_id":"1","content":"hi","nonce":"abc"}`,
	}, "\n")
	var buf bytes.Buffer
	r := &recorder{}
	Run(strings.NewReader(in), NewEncoder(&buf), r)

	if !r.closed {
		t.Error("handler not closed at eof")
	}
	if len(r.cmds) != 2 || r.cmds[0].Cmd != "start" || r.cmds[1].Nonce != "abc" || r.cmds[1].Content != "hi" {
		t.Fatalf("commands = %+v", r.cmds)
	}

	evs := events(t, buf.String())
	if len(evs) != 2 {
		t.Fatalf("events = %v", evs)
	}
	if evs[0]["ev"] != "error" || evs[0]["cmd"] != "nope" {
		t.Errorf("unknown command gave %v", evs[0])
	}
	if evs[1]["ev"] != "error" || evs[1]["cmd"] != "" {
		t.Errorf("bad json gave %v", evs[1])
	}
}

func TestRunHandlerError(t *testing.T) {
	var buf bytes.Buffer
	Run(strings.NewReader(`{"cmd":"open_guild","guild_id":"x"}`), NewEncoder(&buf), &recorder{fail: errors.New("unknown guild")})

	evs := events(t, buf.String())
	if len(evs) != 1 || evs[0]["ev"] != "error" || evs[0]["cmd"] != "open_guild" || evs[0]["msg"] != "unknown guild" {
		t.Fatalf("events = %v", evs)
	}
}
