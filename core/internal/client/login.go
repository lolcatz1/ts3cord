// parts derived from discordo (github.com/ayn2op/discordo), gpl-3.0

package client

import (
	"context"
	"errors"
	"log/slog"

	"github.com/ayn2op/arikawa/v3/api"
	"github.com/ayn2op/arikawa/v3/utils/httputil"
	"github.com/zalando/go-keyring"

	"ts3cord/core/internal/http"
	"ts3cord/core/internal/proto"
)

const (
	keyringService = "ts3cord"
	keyringUser    = "token"
)

func getToken() (string, error) {
	return keyring.Get(keyringService, keyringUser)
}

func setToken(token string) {
	if err := keyring.Set(keyringService, keyringUser, token); err != nil {
		slog.Error("failed to set token to keyring", "err", err)
	}
}

func deleteToken() {
	if err := keyring.Delete(keyringService, keyringUser); err != nil && !errors.Is(err, keyring.ErrNotFound) {
		slog.Error("failed to delete token from keyring", "err", err)
	}
}

// loginClient is a client without a token, with the headers of the login page.
func loginClient(fingerprint string) *api.Client {
	headers := http.Headers()
	headers.Set("Referer", "https://discord.com/login")
	if fingerprint != "" {
		headers.Set("X-Fingerprint", fingerprint)
	}

	client := http.NewClient("")
	client.OnRequest = append(client.OnRequest, httputil.WithHeaders(headers))
	return client
}

// errorMessage is what discord said went wrong, or err.
func errorMessage(err error) string {
	if httpErr, ok := errors.AsType[*httputil.HTTPError](err); ok && httpErr.Message != "" {
		return httpErr.Message
	}
	return err.Error()
}

func (c *Client) loginError(err error) {
	slog.Warn("login failed", "err", err)
	c.out.Emit("login_error", proto.F{"msg": errorMessage(err)})
}

func (c *Client) loginPassword(login, password string) {
	if login == "" || password == "" {
		c.out.Emit("login_error", proto.F{"msg": "login and password are required"})
		return
	}

	resp, err := loginClient("").Login(login, password)
	if err != nil {
		c.loginError(err)
		return
	}
	c.loginResponse(resp)
}

func (c *Client) loginResponse(resp *api.LoginResponse) {
	if resp.Token != "" {
		c.connect(resp.Token, true)
		return
	}
	if !resp.MFA || resp.Ticket == "" {
		c.loginError(errors.New("login response did not include a token"))
		return
	}

	c.mu.Lock()
	c.mfa[resp.Ticket] = resp.LoginInstanceID
	c.mu.Unlock()

	methods := []string{}
	if resp.TOTP {
		methods = append(methods, "totp")
	}
	if resp.Backup {
		methods = append(methods, "backup")
	}
	if resp.SMS {
		methods = append(methods, "sms")
	}
	c.out.Emit("mfa_required", proto.F{"ticket": resp.Ticket, "methods": methods})
}

func (c *Client) loginMFA(ticket, code, method string) {
	c.mu.Lock()
	instanceID, ok := c.mfa[ticket]
	c.mu.Unlock()
	if !ok {
		c.out.Emit("login_error", proto.F{"msg": "unknown mfa ticket, log in again"})
		return
	}

	var resp *api.LoginResponse
	var err error
	switch method {
	// the totp endpoint takes backup codes too.
	case "", "totp", "backup":
		resp, err = loginClient("").TOTP(code, ticket, instanceID)
	default:
		err = errors.New("unsupported mfa method " + method)
	}
	if err != nil {
		c.loginError(err)
		return
	}

	c.mu.Lock()
	delete(c.mfa, ticket)
	c.mu.Unlock()
	c.loginResponse(resp)
}

func (c *Client) startQR() {
	ctx, cancel := context.WithCancel(context.Background())

	c.mu.Lock()
	if c.cancelLogin != nil {
		c.cancelLogin()
	}
	c.cancelLogin = cancel
	c.mu.Unlock()

	c.wg.Go(func() {
		defer cancel()
		token, err := remoteAuth(ctx,
			func(url string) { c.out.Emit("qr", proto.F{"url": url}) },
			func(username, avatarURL string) {
				c.out.Emit("qr_user", proto.F{"username": username, "avatar_url": avatarURL})
			},
		)
		if ctx.Err() != nil {
			// canceled by logout, another qr_start or exit.
			return
		}
		if err != nil {
			c.loginError(err)
			return
		}
		c.connect(token, true)
	})
}
