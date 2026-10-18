package demo

import (
	"strconv"

	"ts3cord/core/internal/proto"
)

// fake world of --demo. nothing here touches the network.

const (
	meID = "300000000000000001"

	guildQuake = "200000000000000001"
	guildCats  = "200000000000000002"
	guildLinux = "200000000000000003"
)

type fakeUser struct {
	id, username, global string
	avatar               int
	bot                  bool
	status, activity     string
	bio                  string
	created              string
}

var users = []fakeUser{
	{meID, "leroy", "leroy", 0, false, "online", "", "at least i have chicken", "2015-05-13T19:02:11Z"},
	{"300000000000000002", "dave", "Dave", 1, false, "online", "Counter-Strike 1.6", "lan party organizer since 2003", "2016-02-01T10:00:00Z"},
	{"300000000000000003", "xxsniperxx", "Sn1per", 2, false, "dnd", "Quake III Arena", "1v1 me rail only", "2016-07-21T22:31:00Z"},
	{"300000000000000004", "mirc_mike", "Mike", 3, false, "idle", "", "/join #linux", "2015-11-11T11:11:11Z"},
	{"300000000000000005", "kevin", "Kevin", 4, false, "online", "Spotify", "", "2017-03-03T08:15:00Z"},
	{"300000000000000006", "tanya", "Tanya", 5, false, "online", "", "cats > dogs", "2018-09-09T09:09:09Z"},
	{"300000000000000007", "noobslayer", "NoobSlayer", 0, false, "online", "Unreal Tournament 2004", "", "2019-01-01T00:00:01Z"},
	{"300000000000000008", "jenkins", "Jenkins", 1, false, "online", "World of Warcraft", "LEEEROOOY", "2016-05-11T17:45:00Z"},
	{"300000000000000009", "pixel", "Pixel", 2, false, "idle", "Aseprite", "i draw small things", "2020-04-20T14:00:00Z"},
	{"300000000000000010", "shaun", "Shaun", 3, false, "online", "", "", "2017-12-24T23:59:00Z"},
	{"300000000000000011", "bob_the_builder", "bob", 4, false, "offline", "", "can we fix it", "2018-02-14T12:00:00Z"},
	{"300000000000000012", "carl", "Carl", 5, false, "online", "", "", "2019-06-30T06:30:00Z"},
	{"300000000000000013", "sarah", "Sarah", 0, false, "online", "Visual Studio Code", "kernel hacker", "2016-10-10T10:10:10Z"},
	{"300000000000000014", "geo", "geo", 1, false, "online", "", "", "2021-08-08T08:08:08Z"},
	{"300000000000000015", "deskbot", "DeskBot", 2, true, "online", "", "beep boop. type !help", "2020-01-01T00:00:00Z"},
}

const (
	uDave   = "300000000000000002"
	uSniper = "300000000000000003"
	uMike   = "300000000000000004"
	uKevin  = "300000000000000005"
	uTanya  = "300000000000000006"
	uNoob   = "300000000000000007"
	uJenk   = "300000000000000008"
	uPixel  = "300000000000000009"
	uShaun  = "300000000000000010"
	uBob    = "300000000000000011"
	uCarl   = "300000000000000012"
	uSarah  = "300000000000000013"
	uGeo    = "300000000000000014"
	uBot    = "300000000000000015"
)

type fakeMember struct {
	nick   string
	roles  []string
	joined string
}

type fakeGuild struct {
	info     proto.GuildFull
	roles    []proto.Role
	channels []proto.Channel
	members  map[string]fakeMember
}

// channel ids are 4000000000000000gg + nn.
func chID(guild, n int) string {
	return strconv.Itoa(400000000000000000 + guild*100 + n)
}

func cat(guild, n, pos int, name string) proto.Channel {
	return proto.Channel{ID: chID(guild, n), Type: 4, Name: name, Position: pos, CanView: true, CanSend: true, CanConnect: true}
}

func text(guild, n, pos, parent int, name, topic string) proto.Channel {
	c := proto.Channel{ID: chID(guild, n), Type: 0, Name: name, Position: pos, Topic: topic, CanView: true, CanSend: true, CanConnect: true}
	if parent >= 0 {
		c.ParentID = chID(guild, parent)
	}
	return c
}

func voiceCh(guild, n, pos, parent int, name string, limit int) proto.Channel {
	c := proto.Channel{ID: chID(guild, n), Type: 2, Name: name, Position: pos, Bitrate: 64000, UserLimit: limit, CanView: true, CanSend: true, CanConnect: true}
	if parent >= 0 {
		c.ParentID = chID(guild, parent)
	}
	return c
}

func readOnly(c proto.Channel) proto.Channel { c.CanSend = false; return c }
func hidden(c proto.Channel) proto.Channel {
	c.CanView, c.CanSend, c.CanConnect = false, false, false
	return c
}
func noConnect(c proto.Channel) proto.Channel {
	c.CanConnect = false
	return c
}
func nsfw(c proto.Channel) proto.Channel { c.NSFW = true; return c }

func role(id, name, color string, pos int, hoist bool) proto.Role {
	return proto.Role{ID: id, Name: name, Color: color, Position: pos, Hoist: hoist}
}

func guilds() []*fakeGuild {
	quake := &fakeGuild{
		info: proto.GuildFull{ID: guildQuake, Name: "Quake Clan [QC]", OwnerID: uDave, Description: "frag hard, frag often. clan wars every friday.", MemberCount: 12},
		roles: []proto.Role{
			role(guildQuake, "@everyone", "", 0, false),
			role("500000000000000101", "Clan Leader", "#e74c3c", 5, true),
			role("500000000000000102", "Officer", "#e67e22", 4, true),
			role("500000000000000103", "Member", "#3498db", 3, true),
			role("500000000000000104", "Recruit", "#95a5a6", 2, false),
			role("500000000000000105", "Bots", "#9b59b6", 1, true),
		},
		channels: []proto.Channel{
			cat(1, 1, 0, "Information"),
			readOnly(text(1, 2, 0, 1, "rules", "read before you post")),
			readOnly(text(1, 3, 1, 1, "announcements", "clan news")),
			cat(1, 4, 1, "Text Channels"),
			text(1, 5, 0, 4, "general", "talk about anything"),
			text(1, 6, 1, 4, "off-topic", ""),
			text(1, 7, 2, 4, "screenshots", "frags or it didn't happen"),
			nsfw(text(1, 8, 3, 4, "nsfw", "keep it legal")),
			cat(1, 9, 2, "Voice Channels"),
			voiceCh(1, 10, 0, 9, "Lobby", 0),
			voiceCh(1, 11, 1, 9, "Clan War #1", 5),
			voiceCh(1, 12, 2, 9, "Clan War #2", 5),
			voiceCh(1, 13, 3, 9, "Music", 0),
			voiceCh(1, 14, 4, 9, "AFK", 0),
			cat(1, 15, 3, "Officers Only"),
			hidden(text(1, 16, 0, 15, "officer-chat", "")),
			noConnect(voiceCh(1, 17, 1, 15, "Officer Room", 0)),
		},
		members: map[string]fakeMember{
			meID:    {"LeroyJ", []string{"500000000000000103"}, "2019-02-02T20:00:00Z"},
			uDave:   {"[QC] Dave", []string{"500000000000000101"}, "2016-02-02T10:00:00Z"},
			uSniper: {"[QC] Sn1per", []string{"500000000000000102", "500000000000000103"}, "2017-01-01T12:00:00Z"},
			uKevin:  {"", []string{"500000000000000103"}, "2018-03-03T12:00:00Z"},
			uNoob:   {"", []string{"500000000000000104"}, "2021-07-07T12:00:00Z"},
			uJenk:   {"[QC] Jenkins", []string{"500000000000000102"}, "2017-05-05T12:00:00Z"},
			uPixel:  {"", []string{"500000000000000103"}, "2020-05-05T12:00:00Z"},
			uBob:    {"", nil, "2022-01-01T12:00:00Z"},
			uBot:    {"", []string{"500000000000000105"}, "2020-01-02T12:00:00Z"},
		},
	}

	cats := &fakeGuild{
		info: proto.GuildFull{ID: guildCats, Name: "lolcats united", OwnerID: uTanya, Description: "i can has server?", MemberCount: 7},
		roles: []proto.Role{
			role(guildCats, "@everyone", "", 0, false),
			role("500000000000000201", "Ceiling Cat", "#f1c40f", 3, true),
			role("500000000000000202", "Kitteh", "#1abc9c", 2, true),
			role("500000000000000203", "Mods", "#2ecc71", 1, true),
		},
		channels: []proto.Channel{
			readOnly(text(2, 1, 0, -1, "welcome", "oh hai")),
			hidden(text(2, 2, 1, -1, "mod-log", "")),
			cat(2, 3, 0, "chat"),
			text(2, 4, 0, 3, "general", "invisible bike goes here"),
			text(2, 5, 1, 3, "memes", "moar cheezburgers"),
			text(2, 6, 2, 3, "pets", "post ur cats"),
			cat(2, 7, 1, "voice"),
			voiceCh(2, 8, 0, 7, "Chill Zone", 0),
			voiceCh(2, 9, 1, 7, "Gaming", 4),
			voiceCh(2, 10, 2, 7, "Movie Night", 10),
		},
		members: map[string]fakeMember{
			meID:   {"", []string{"500000000000000202"}, "2020-02-02T20:00:00Z"},
			uTanya: {"Ceiling Tanya", []string{"500000000000000201"}, "2018-09-10T12:00:00Z"},
			uCarl:  {"", []string{"500000000000000202", "500000000000000203"}, "2019-07-01T12:00:00Z"},
			uShaun: {"basement cat", []string{"500000000000000202"}, "2018-01-01T12:00:00Z"},
			uPixel: {"", []string{"500000000000000202"}, "2020-04-21T12:00:00Z"},
			uKevin: {"", nil, "2021-02-02T12:00:00Z"},
			uDave:  {"", []string{"500000000000000202"}, "2019-02-03T12:00:00Z"},
		},
	}

	linux := &fakeGuild{
		info: proto.GuildFull{ID: guildLinux, Name: "Linux Help Desk", OwnerID: uSarah, Description: "have you tried turning it off and on again", MemberCount: 9},
		roles: []proto.Role{
			role(guildLinux, "@everyone", "", 0, false),
			role("500000000000000301", "Admin", "#c0392b", 4, true),
			role("500000000000000302", "Helper", "#27ae60", 3, true),
			role("500000000000000303", "Bot", "#8e44ad", 2, true),
			role("500000000000000304", "Arch User (btw)", "#1793d1", 1, false),
		},
		channels: []proto.Channel{
			cat(3, 1, 0, "help"),
			text(3, 2, 0, 1, "beginners", "no question is too dumb"),
			text(3, 3, 1, 1, "arch", "i use arch btw"),
			text(3, 4, 2, 1, "wine-gaming", "proton, wine, dxvk"),
			cat(3, 5, 1, "voice"),
			voiceCh(3, 6, 0, 5, "Help Desk 1", 2),
			voiceCh(3, 7, 1, 5, "Help Desk 2", 2),
			voiceCh(3, 8, 2, 5, "Hangout", 0),
			hidden(voiceCh(3, 9, 3, 5, "Staff Only", 0)),
		},
		members: map[string]fakeMember{
			meID:    {"", []string{"500000000000000304"}, "2021-03-03T20:00:00Z"},
			uSarah:  {"", []string{"500000000000000301"}, "2016-10-11T12:00:00Z"},
			uMike:   {"mike (helper)", []string{"500000000000000302"}, "2016-11-01T12:00:00Z"},
			uGeo:    {"", []string{"500000000000000304"}, "2021-08-09T12:00:00Z"},
			uBot:    {"", []string{"500000000000000303"}, "2020-01-02T12:00:00Z"},
			uSniper: {"", nil, "2022-02-02T12:00:00Z"},
			uJenk:   {"", []string{"500000000000000304"}, "2021-04-04T12:00:00Z"},
			uCarl:   {"", nil, "2022-03-03T12:00:00Z"},
		},
	}

	gs := []*fakeGuild{quake, cats, linux}
	for _, g := range gs {
		for i := range g.channels {
			g.channels[i].GuildID = g.info.ID
		}
	}
	return gs
}

type fakeVoice struct {
	guild, channel, user string
	selfMute, selfDeaf   bool
	mute                 bool
}

func voiceStates() []fakeVoice {
	return []fakeVoice{
		{guildQuake, chID(1, 10), uDave, false, false, false},
		{guildQuake, chID(1, 10), uKevin, true, false, false},
		{guildQuake, chID(1, 11), uSniper, false, false, false},
		{guildQuake, chID(1, 11), uNoob, false, false, false},
		{guildQuake, chID(1, 11), uJenk, true, true, false},
		{guildQuake, chID(1, 13), uPixel, false, false, false},
		{guildQuake, chID(1, 14), uBob, true, true, false},
		{guildCats, chID(2, 8), uTanya, false, false, false},
		{guildCats, chID(2, 8), uCarl, true, false, false},
		{guildCats, chID(2, 9), uShaun, false, false, false},
		{guildLinux, chID(3, 6), uMike, false, false, false},
		{guildLinux, chID(3, 6), uSarah, false, false, false},
		{guildLinux, chID(3, 8), uGeo, false, false, true},
	}
}

type fakeDM struct {
	id         string
	name       string
	recipients []string
	group      bool
	muted      bool
}

var dms = []fakeDM{
	{"600000000000000001", "", []string{uDave}, false, false},
	{"600000000000000002", "", []string{uTanya}, false, false},
	{"600000000000000003", "LAN party", []string{uDave, uKevin, uPixel}, true, true},
	{"600000000000000004", "", []string{uMike}, false, false},
}

// relationship types as discord numbers them.
var friends = []struct {
	user    string
	typ     int
	ignored bool
}{
	{uDave, 1, false}, {uTanya, 1, false}, {uMike, 1, true}, {uKevin, 1, false}, {uGeo, 3, false}, {uNoob, 2, false},
}

// history lines of fake messages. {me} mentions leroy, {u} some other member.
var lines = []string{
	"anyone up for a match tonight?",
	"lol",
	"brb dinner",
	"gg wp everyone",
	"who took my spot in lobby",
	"server restart in **5 minutes**, save your stuff",
	"check #announcements before asking pls",
	"`sudo rm -rf /` fixes everything (don't)",
	"> ventrilo was better\nno it wasn't",
	"||the cake is a lie||",
	"https://www.youtube.com/watch?v=dQw4w9WgXcQ",
	"has anyone seen my headset",
	"my ping is like 300 rn, isp is being dumb",
	"*sigh* lag again",
	"new map rotation is up",
	"```\n[QC] Dave: !ban noobslayer\n[DeskBot]: nope\n```",
	"ok who left the mic open, i can hear a vacuum",
	"i'll be on around 9",
	"{u} you coming?",
	"anyone have a spare key for the beta?",
	"__underline__ test, does that render",
	"~~strikethrough~~ nvm",
	"lmao",
	"rofl",
	"brb, mom needs the phone line",
	"that was the cleanest rocket jump i've ever seen",
	"pushed the config to the wiki",
	"night all",
	"morning",
	"did the patch break anyone else's mouse accel?",
	"i blame the netcode",
	"🔥🔥🔥",
	"same",
	"can someone move me to the war channel",
	"pizza's here",
	"### reminder\nclan war friday 21:00, be there",
	"1. warm up\n2. strat talk\n3. win",
	"i'm muted, one sec",
	"teamspeak > everything",
	"nice",
}

// live lines of the occasional incoming message.
var liveLines = []string{
	"anyone around?",
	"jumping in voice in 5",
	"yo {me} you on tonight?",
	"that last round was insane",
	"brb",
	"who's up for a quick one",
	"lol same",
	"just patched, downloading 2gb ugh",
	"**gg**",
	"`/msg nickserv identify` wrong window oops",
}

var dmLines = []string{
	"hey",
	"you around?",
	"did you get the files i sent",
	"lol yeah",
	"talk later",
	"ok cool",
	"np",
	"send me the server ip again",
}

// fake audio devices of voice_devices.
var (
	fakeInputs  = []string{"Default", "Blue Yeti USB Microphone", "Built-in Audio Analog Stereo"}
	fakeOutputs = []string{"Default", "Logitech G430 Headset", "Built-in Audio Analog Stereo"}
)
