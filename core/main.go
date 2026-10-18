// ts3cord-core is the discord backend of ts3cord. it reads commands from stdin and writes events to stdout, one json object per line.
package main

import (
	"flag"
	"log/slog"
	"os"

	"ts3cord/core/internal/client"
	"ts3cord/core/internal/demo"
	"ts3cord/core/internal/proto"
)

func main() {
	demoMode := flag.Bool("demo", false, "fake data, no network")
	debug := flag.Bool("debug", false, "debug logs")
	flag.Parse()

	level := slog.LevelInfo
	if *debug {
		level = slog.LevelDebug
	}
	slog.SetDefault(slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: level})))

	out := proto.NewEncoder(os.Stdout)
	var h proto.Handler
	if *demoMode {
		slog.Info("demo mode")
		h = demo.New(out, demo.DefaultOptions())
	} else {
		h = client.New(out)
	}

	proto.Run(os.Stdin, out, h)
	slog.Info("stdin closed, exiting")
}
