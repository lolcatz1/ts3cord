// derived from discordo (github.com/ayn2op/discordo) internal/ui/login/qr, gpl-3.0

package client

import (
	"context"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/x509"
	"encoding/base64"
	json "encoding/json/v2"
	"errors"
	"strings"
	"sync"
	"time"

	"github.com/ayn2op/arikawa/v3/discord"
	"github.com/gorilla/websocket"

	"ts3cord/core/internal/gateway"
	"ts3cord/core/internal/http"
)

const remoteAuthGatewayURL = "wss://remote-auth-gateway.discord.gg/?v=2"

// gatewayConn is a remote auth gateway connection whose writes are serialized, since a websocket.Conn takes one writer at a time.
type gatewayConn struct {
	ws      *websocket.Conn
	writeMu sync.Mutex
}

func (c *gatewayConn) writeJSON(v any) error {
	c.writeMu.Lock()
	defer c.writeMu.Unlock()
	return c.ws.WriteJSON(v)
}

type remoteAuthPayload struct {
	Op                   string `json:"op"`
	HeartbeatInterval    int    `json:"heartbeat_interval"`
	TimeoutMS            int    `json:"timeout_ms"`
	EncryptedNonce       string `json:"encrypted_nonce"`
	Fingerprint          string `json:"fingerprint"`
	EncryptedUserPayload string `json:"encrypted_user_payload"`
	Ticket               string `json:"ticket"`
}

func decrypt(key *rsa.PrivateKey, encoded string) ([]byte, error) {
	data, err := base64.StdEncoding.DecodeString(encoded)
	if err != nil {
		return nil, err
	}
	return rsa.DecryptOAEP(sha256.New(), nil, key, data, nil)
}

// remoteAuth logs in by a qr code scanned with the mobile app and returns the token. onQR gets the url of the code, onUser who scanned it.
func remoteAuth(ctx context.Context, onQR func(url string), onUser func(username, avatarURL string)) (string, error) {
	headers := http.Headers()
	headers.Set("User-Agent", http.BrowserUserAgent())
	dialer := gateway.NewDialer()
	ws, _, err := dialer.DialContext(ctx, remoteAuthGatewayURL, headers)
	if err != nil {
		return "", err
	}
	conn := &gatewayConn{ws: ws}

	done := make(chan struct{})
	defer close(done)
	go func() {
		select {
		case <-ctx.Done():
		case <-done:
		}
		ws.Close()
	}()

	var (
		key         *rsa.PrivateKey
		fingerprint string
	)
	for {
		_, data, err := ws.ReadMessage()
		if err != nil {
			if ctx.Err() != nil {
				return "", ctx.Err()
			}
			return "", err
		}

		var payload remoteAuthPayload
		if err := json.Unmarshal(data, &payload); err != nil {
			return "", err
		}

		switch payload.Op {
		case "hello":
			go heartbeat(conn, time.Duration(payload.HeartbeatInterval)*time.Millisecond, done)

			if key, err = rsa.GenerateKey(rand.Reader, 2048); err != nil {
				return "", err
			}
			spki, err := x509.MarshalPKIXPublicKey(key.Public())
			if err != nil {
				return "", err
			}
			if err := conn.writeJSON(struct {
				Op               string `json:"op"`
				EncodedPublicKey string `json:"encoded_public_key"`
			}{"init", base64.StdEncoding.EncodeToString(spki)}); err != nil {
				return "", err
			}

		case "nonce_proof":
			if key == nil {
				return "", errors.New("missing private key")
			}
			nonce, err := decrypt(key, payload.EncryptedNonce)
			if err != nil {
				return "", err
			}
			if err := conn.writeJSON(struct {
				Op    string `json:"op"`
				Nonce string `json:"nonce"`
			}{"nonce_proof", base64.RawURLEncoding.EncodeToString(nonce)}); err != nil {
				return "", err
			}

		case "pending_remote_init":
			fingerprint = payload.Fingerprint
			onQR("https://discord.com/ra/" + fingerprint)

		case "pending_ticket":
			if key == nil {
				return "", errors.New("missing private key")
			}
			user, err := decrypt(key, payload.EncryptedUserPayload)
			if err != nil {
				return "", err
			}
			// id:discriminator:avatar:username
			parts := strings.Split(string(user), ":")
			if len(parts) != 4 {
				return "", errors.New("invalid user payload")
			}
			u := discord.User{Username: parts[3], Discriminator: parts[1], Avatar: discord.Hash(parts[2])}
			if userID, err := discord.ParseSnowflake(parts[0]); err == nil {
				u.ID = discord.UserID(userID)
			}
			onUser(u.Username, avatarURL(u))

		case "pending_login":
			if key == nil {
				return "", errors.New("missing private key")
			}
			encrypted, err := loginClient(fingerprint).ExchangeRemoteAuthTicket(payload.Ticket)
			if err != nil {
				return "", err
			}
			token, err := decrypt(key, encrypted)
			if err != nil {
				return "", err
			}
			return string(token), nil

		case "cancel":
			return "", errors.New("login canceled on mobile")
		}
	}
}

func heartbeat(conn *gatewayConn, interval time.Duration, done <-chan struct{}) {
	if interval <= 0 {
		return
	}
	ticker := time.NewTicker(interval)
	defer ticker.Stop()
	for {
		select {
		case <-done:
			return
		case <-ticker.C:
			if err := conn.writeJSON(struct {
				Op string `json:"op"`
			}{"heartbeat"}); err != nil {
				return
			}
		}
	}
}
