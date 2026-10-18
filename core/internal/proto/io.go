package proto

import (
	"bufio"
	"bytes"
	json "encoding/json/v2"
	"io"
	"log/slog"
	"sync"
)

// encoder writes one event per line. it is safe for concurrent use.
type Encoder struct {
	mu sync.Mutex
	w  io.Writer
}

func NewEncoder(w io.Writer) *Encoder {
	return &Encoder{w: w}
}

func (e *Encoder) Emit(ev string, fields F) {
	out := make(F, len(fields)+1)
	for k, v := range fields {
		out[k] = v
	}
	out["ev"] = ev

	data, err := json.Marshal(out, json.Deterministic(true))
	if err != nil {
		slog.Error("failed to marshal event", "ev", ev, "err", err)
		return
	}
	data = append(data, '\n')

	e.mu.Lock()
	defer e.mu.Unlock()
	if _, err := e.w.Write(data); err != nil {
		slog.Error("failed to write event", "ev", ev, "err", err)
	}
}

// error emits the error event of cmd.
func Error(out Emitter, cmd string, err error) {
	out.Emit("error", F{"msg": err.Error(), "cmd": cmd})
}

// decode parses one command line.
func Decode(line []byte) (Command, error) {
	var cmd Command
	err := json.Unmarshal(line, &cmd)
	return cmd, err
}

// run reads commands from r, one per line, and hands them to h until r ends.
func Run(r io.Reader, out Emitter, h Handler) {
	defer h.Close()

	scanner := bufio.NewScanner(r)
	scanner.Buffer(make([]byte, 64*1024), 16*1024*1024)
	for scanner.Scan() {
		line := bytes.TrimSpace(scanner.Bytes())
		if len(line) == 0 {
			continue
		}

		cmd, err := Decode(line)
		if err != nil {
			Error(out, "", err)
			continue
		}
		if !Commands[cmd.Cmd] {
			out.Emit("error", F{"msg": "unknown command", "cmd": cmd.Cmd})
			continue
		}

		slog.Debug("command", "cmd", cmd.Cmd)
		if err := h.Handle(cmd); err != nil {
			slog.Warn("command failed", "cmd", cmd.Cmd, "err", err)
			Error(out, cmd.Cmd, err)
		}
	}

	if err := scanner.Err(); err != nil {
		slog.Error("failed to read stdin", "err", err)
	}
}
