// Package proto is the json lines protocol between the ui and the core, see docs/PROTOCOL.md.
package proto

// user is a discord user as the ui shows it.
type User struct {
	ID         string `json:"id"`
	Username   string `json:"username"`
	GlobalName string `json:"global_name"`
	Display    string `json:"display"`
	AvatarURL  string `json:"avatar_url"`
	Bot        bool   `json:"bot"`
}

type Channel struct {
	// muted: the channel (or dm) is muted in the user's notification settings.
	Muted         bool   `json:"muted"`
	ID            string `json:"id"`
	GuildID       string `json:"guild_id"`
	Type          int    `json:"type"`
	Name          string `json:"name"`
	Position      int    `json:"position"`
	ParentID      string `json:"parent_id"`
	Topic         string `json:"topic"`
	NSFW          bool   `json:"nsfw"`
	Bitrate       int    `json:"bitrate"`
	UserLimit     int    `json:"user_limit"`
	CanView       bool   `json:"can_view"`
	CanSend       bool   `json:"can_send"`
	CanConnect    bool   `json:"can_connect"`
	LastMessageID string `json:"last_message_id"`
	Unread        bool   `json:"unread"`
	Mentions      int    `json:"mentions"`
	Recipients    []User `json:"recipients,omitempty"`
}

type Attachment struct {
	Filename    string `json:"filename"`
	URL         string `json:"url"`
	Size        int    `json:"size"`
	ContentType string `json:"content_type"`
	Width       int    `json:"width"`
	Height      int    `json:"height"`
}

type Embed struct {
	Title       string `json:"title"`
	Description string `json:"description"`
	URL         string `json:"url"`
	Author      string `json:"author"`
	Footer      string `json:"footer"`
}

type Reply struct {
	ID      string `json:"id"`
	Author  User   `json:"author"`
	Content string `json:"content"`
}

type Message struct {
	ID              string       `json:"id"`
	ChannelID       string       `json:"channel_id"`
	GuildID         string       `json:"guild_id"`
	Type            int          `json:"type"`
	Author          User         `json:"author"`
	AuthorColor     string       `json:"author_color"`
	Content         string       `json:"content"`
	Timestamp       string       `json:"timestamp"`
	EditedTimestamp string       `json:"edited_timestamp"`
	MentionsMe      bool         `json:"mentions_me"`
	Attachments     []Attachment `json:"attachments"`
	Embeds          []Embed      `json:"embeds"`
	ReplyTo         *Reply       `json:"reply_to"`
}

type VoiceState struct {
	GuildID   string `json:"guild_id"`
	ChannelID string `json:"channel_id"`
	User      User   `json:"user"`
	SelfMute  bool   `json:"self_mute"`
	SelfDeaf  bool   `json:"self_deaf"`
	Mute      bool   `json:"mute"`
	Deaf      bool   `json:"deaf"`
	Streaming bool   `json:"streaming"`
	Video     bool   `json:"video"`
	Suppress  bool   `json:"suppress"`
}

// guild is the short guild of ready.
type Guild struct {
	ID          string `json:"id"`
	Name        string `json:"name"`
	IconURL     string `json:"icon_url"`
	OwnerID     string `json:"owner_id"`
	MemberCount int    `json:"member_count"`
}

// guildfull is the guild of guild_snapshot, guild_create and guild_update.
type GuildFull struct {
	ID          string `json:"id"`
	Name        string `json:"name"`
	IconURL     string `json:"icon_url"`
	OwnerID     string `json:"owner_id"`
	Description string `json:"description"`
	MemberCount int    `json:"member_count"`
	BannerURL   string `json:"banner_url"`
}

type Role struct {
	ID       string `json:"id"`
	Name     string `json:"name"`
	Color    string `json:"color"`
	Position int    `json:"position"`
	Hoist    bool   `json:"hoist"`
}

type ProfileRole struct {
	Name  string `json:"name"`
	Color string `json:"color"`
}

type Me struct {
	Nick  string   `json:"nick"`
	Roles []string `json:"roles"`
}

type Friend struct {
	User User `json:"user"`
	Type int  `json:"type"`
	// ignored is discord's "ignore" (no notifications, hidden dms), separate from blocking.
	Ignored bool `json:"ignored"`
}

// command is one line of stdin. fields not used by cmd are zero.
type Command struct {
	Cmd string `json:"cmd"`

	Token    string `json:"token"`
	Login    string `json:"login"`
	Password string `json:"password"`
	Ticket   string `json:"ticket"`
	Code     string `json:"code"`
	Method   string `json:"method"`

	GuildID   string `json:"guild_id"`
	ChannelID string `json:"channel_id"`
	MessageID string `json:"message_id"`
	UserID    string `json:"user_id"`
	Before    string `json:"before"`
	Limit     int    `json:"limit"`
	Content   string `json:"content"`
	Nonce     string `json:"nonce"`
	ReplyTo   string `json:"reply_to"`
	// local file paths to upload with send_message
	Files []string `json:"files"`

	Mute   bool   `json:"mute"`
	Deaf   bool   `json:"deaf"`
	Status string `json:"status"`

	InputDevice  *string  `json:"input_device"`
	OutputDevice *string  `json:"output_device"`
	Threshold    *float64 `json:"threshold"`
}

// commands are the cmds of the protocol.
var Commands = map[string]bool{
	"start":          true,
	"login_token":    true,
	"login_password": true,
	"login_mfa":      true,
	"qr_start":       true,
	"logout":         true,
	"open_guild":     true,
	"fetch_messages": true,
	"send_message":   true,
	"edit_message":   true,
	"delete_message": true,
	"typing":         true,
	"ack":            true,
	"join_voice":     true,
	"leave_voice":    true,
	"set_self_mute":  true,
	"set_self_deaf":  true,
	"set_status":     true,
	"user_profile":   true,
	"open_dm":        true,
	"voice_settings": true,
}

// f is the fields of an event.
type F = map[string]any

// emitter writes events.
type Emitter interface {
	Emit(ev string, fields F)
}

// handler runs commands. handle is called for one command at a time, in order.
type Handler interface {
	Handle(cmd Command) error
	Close()
}
