// derived from discordo (github.com/ayn2op/discordo), gpl-3.0

//go:build no_spoof_tls_fingerprint

package gateway

import "testing"

func TestNewDialer(t *testing.T) {
	dialer := NewDialer()
	if dialer.NetDialTLSContext != nil {
		t.Error("custom TLS dialer is enabled")
	}
	if dialer.Proxy == nil {
		t.Error("environment proxy is disabled")
	}
	if !dialer.EnableCompression {
		t.Error("compression is disabled")
	}
}
