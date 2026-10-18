// derived from discordo (github.com/ayn2op/discordo), gpl-3.0

package gateway

import (
	"github.com/ayn2op/arikawa/v3/api"
	discordgateway "github.com/ayn2op/arikawa/v3/gateway"
	"github.com/ayn2op/arikawa/v3/utils/ws"
	"ts3cord/core/internal/http"
)

const gatewayURL = "wss://gateway.discord.gg"

func New(id discordgateway.Identifier) *discordgateway.Gateway {
	codec := ws.NewCodec(discordgateway.NewOpUnmarshalers(id.Capabilities))
	codec.Headers.Set("Origin", api.BaseEndpoint)
	codec.Headers.Set("User-Agent", http.BrowserUserAgent())

	conn := ws.NewConnWithDialer(codec, NewDialer())
	socket := ws.NewCustomWebsocket(conn, discordgateway.AddGatewayParams(gatewayURL))
	return discordgateway.FromWebsocketGateway(ws.NewGateway(socket, &discordgateway.DefaultGatewayOpts), discordgateway.State{Identifier: id})
}
