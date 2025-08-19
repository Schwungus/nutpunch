# nutpunch

> [!WARNING]
> Using this library and implementing UDP hole-punching **only** makes sense if you're **making a P2P networked game**. If you need server-based networking **(which you should use instead)**, you're out of luck -- go elsewhere. You have been warned.

A UDP hole-punching library for the REAL men. Header-only. Brutal. Uses just plain C and winsockets.

## Usage

This library implies P2P networking, where **each client communicates with all others**. It's a complex and, in the wrong hands, a counterproductive model. If you don't feel like reading the immediately following blanket of words and scribbles, you may skip to [using premade integrations](#premade-integrations).

For the sake of simplicity and due to scarcity thereof, we'll assume each client stores a full list of peers, including oneself indexed at 0.

Before you can punch any holes in your peers' NAT, you'll need a hole-punching server **with a public IP address**. Connecting to it lets us bust a port open to the global network, and the server to relay the busted ports to each of its peers. For testing, you can use [our public instance](#public-instance). The current server implementation uses a lobby-based approach, where each lobby supports up to 16 players and is identified by a unique ASCII string.

In order to run your own hole-puncher server, you'll need to get the server binary from our [reference implementation releases](https://github.com/Schwungus/nutpunch/releases/tag/stable). Use [the provided Docker image](https://github.com/Schwungus/nutpunch/pkgs/container/nutpuncher) to host nutpunch server on Linux, since winsock is a hard requirement. If you're in a pinch and don't have a public IP address, and your players reside on a LAN/virtual network such as [Radmin VPN](https://www.radmin-vpn.com/), you can actually run the server locally and use your LAN IP address to connect to it.

Once you've figured out how the players are to connect to your hole-puncher server, you can start coding up your game. [The complete example](src/nutpunchTest.c) might be overwhelming at first, but make sure to skim through it before you do any heavy networking. You might also find some of these[^1][^2] docs and examples useful if you're rawdogging it with winsocks. But here's the general usage guide for the nutpunch library:

1. Join a lobby:
   1. Put a `NutPunch_SetServerAddr("nutpunch-ip-addr")` call to set the hole-puncher server address. You may either use an IPv4 address or the server's DNS hostname, if it has an `A` record set. Use the [public instance address](#public-instance) if you're having a severe brainfart reading all this.
   2. Call `NutPunch_Join("lobby-id")` immediately after `NutPunch_SetServerAddr()` to initiate the joining process.
2. Listen for hole-puncher server responses:
   1. Call `NutPunch_Query()` each frame, regardless of whether you're still joining or already playing with the boys.
   2. Check your status by matching the returned value against `NP_Status_*` constants. `NP_Status_Error` is the only one you should handle explicitly.
3. If all went well, start your match:
   1. You need to synchronize a `NutPunch_Release()` call for all peers involved, once the lobby is full. The simplest way is to do that when the peer count reaches a hardcoded amount, by checking against `NutPunch_GetPeerCount()`.
   2. Use the result of the `NutPunch_Release()` call as the Windows socket for P2P networking. Since it's the only one with a hole punched through it, binding to another socket won't let you have P2P connectivity. I repeat, you **must use the returned socket for P2P**.
4. Run the game logic.
5. Keep in sync with each peer: iterate over `NutPunch_GetPeers()`, starting from index `1` and until `NutPunch_GetPeerCount()`, and use their address/port combo to send/receive datagram updates. In terms of winsock, it means calling `recvfrom()`/`sendto()` and passing the socket you got from `NutPunch_Release()`, as well as the remote peer's `sockaddr` struct. Refer to [the example code](src/nutpunchTest.c) for more info on how to construct it from a `NutPunch_GetPeers()` entry.

[^1]: <https://learn.microsoft.com/en-us/windows/win32/winsock/using-winsock>
[^2]: <https://pastebin.com/JkGnQyPX>

## Premade Integrations

Not written yet. TODO: add a [GekkoNet](https://github.com/HeatXD/GekkoNet/tree/main) network adapter implementation.

## Public Instance

If you don't feel like [hosting your own instance](#hosting-a-nutpuncher-server), you can use our public instance as a kludge. In C code, write:

```c
NutPunch_SetServerAddr("95.163.233.200");
NutPunch_Join("lobby-id");
```

Or if you just want to try the test binary:

```bat
@echo off
.\build\nutpunchTest.exe 2 95.163.233.200
```

## Hosting a Nutpuncher Server

If you're dissatisfied with [the public instance](#public-instance), you can host your own. Make sure to have read [the usage pamphlet](#usage) before attempting this.

On Windows, use [the provided server binary](https://github.com/Schwungus/nutpunch/releases/tag/stable). Make sure port `30001` is open to the public net.

On Linux, **if your system supports Wine**, you can use [our Docker image](https://github.com/Schwungus/nutpunch/pkgs/container/nutpuncher), e.g. with docker-compose:

```yml
services:
  main:
    image: ghcr.io/schwungus/nutpuncher
    container_name: nutpuncher
    network_mode: host # required: just forwarding port 30001 breaks nutpuncher connectivity for some reason
    restart: always
    tty: true # required: the Windows binary doesn't run otherwise
```

## Mental notes

1. `WSAEMSGSIZE` (`10040`) error code is fixed by getting a message buffer of at least `NUTPUNCH_PAYLOAD_SIZE` bytes long. I don't know why the fuck the puncher server keeps sending these payloads.
