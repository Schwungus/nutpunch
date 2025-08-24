# nutpunch

> [!WARNING]
> Using this library and implementing UDP hole-punching **only** makes sense if you're **making a P2P networked game**. If you need server-based networking (**which is arguably much simpler to implement**), you're out of luck -- go elsewhere. You have been warned.

A UDP hole-punching library for REAL men (and women). Header-only. Brutal. Uses plain C and Winsock.

## Introductory Lecture

This library implies P2P networking, where **each peer communicates with all others**. It's a complex and, in the wrong hands, a counterproductive model. If you don't feel like reading the immediately following blanket of words and scribbles, you may skip to [using premade integrations](#premade-integrations).

Before you can punch any holes in your peers' NAT, you'll need a hole-punching server **with a public IP address**. Connecting to it lets us bust a port open to the global network, and the server to relay the busted ports to each of its peers. For testing, you can use [our public instance](#public-instance). The current server implementation uses a lobby-based approach, where each lobby supports up to 16 players and is identified by a unique ASCII string.

In order to run your own hole-puncher server, you'll need to get the server binary from our [reference implementation releases](https://github.com/Schwungus/nutpunch/releases/tag/stable). Use [the provided Docker image](https://github.com/Schwungus/nutpunch/pkgs/container/nutpuncher) to host nutpunch server on Linux, since winsock is a hard requirement. If you're in a pinch and don't have a public IP address, and your players reside on a LAN/virtual network such as [Radmin VPN](https://www.radmin-vpn.com), you can actually run the server locally and use your LAN IP address to connect to it.

Once you've figured out how the players are to connect to your hole-puncher server, you can start coding up your game. [The complete example](src/test.c) might be overwhelming at first, but make sure to skim through it before you do any heavy networking. You might also find some of these[^1][^2] docs and examples useful if you're rawdogging it with winsocks. But here's the general usage guide for the nutpunch library:

1. Join a lobby:
   1. Put a `NutPunch_SetServerAddr("nutpunch-ip-addr")` call to set the hole-puncher server address. You may either use an IPv4 address or the server's DNS hostname, if it has an `A` record set. Use the [public instance address](#public-instance) if you're having a severe brainfart reading all this.
   2. Call `NutPunch_Join("lobby-id")` immediately after `NutPunch_SetServerAddr()` to initiate the joining process.
2. Optionally add metadata to the lobby:
   1. If you join an empty lobby, you will be considered its master. A lobby master has the authority from the server to set global metadata for the lobby. This is usually needed to start games with specific parameters or to enforce an expected player count in a lobby. If you don't need metadata, you can just skip this entire step.
   2. After calling `NutPunch_Join()`, you can set metadata in the lobby by calling `NutPunch_Set()` as a master. A lobby can hold up to 16 fields of metadata, each of which are identified with 8-byte strings and can hold up to 32 bytes of data. Non-masters aren't allowed to set metadata, so that function becomes no-op for them. The actual metadata also won't be updated until the next call to `NutPunch_Query()`, which will be covered later.
3. Listen for hole-puncher server responses:
   1. Call `NutPunch_Query()` each frame, regardless of whether you're still joining or already playing with the boys. This will also automatically update lobby metadata.
   2. Check your status by matching the returned value against `NP_Status_*` constants. `NP_Status_Punched` is the only one you need handle explicitly. `NP_Status_Error` should also be handled, optionally.
4. Optionally read metadata from the lobby. Use `NutPunch_Get()` to get a pointer to a metadata field, which you can then read from (as long as it's valid and is the exact size that you expect it to be). These pointers are volatile, especially when calling `NutPunch_Query()`, so if you need to use the gotten value more than once, cache it somewhere.
5. If all went well, start your match:
   1. Call `NutPunch_Start()` e.g. once your lobby reaches a specific player count. The function will do something only if you're the lobby's master, and it is safe to call otherwise.
   2. Wait for `NutPunch_Query()` to return `NP_Status_Punched`. Now you can call `NutPunch_Done()`.
   3. Use the result of the `NutPunch_Done()` call as the Windows socket for P2P networking. Since it's the only one with a hole punched through it, binding to another socket won't let you have P2P connectivity. I repeat, you **must use the returned socket for P2P**.
6. Run the game logic.
7. Keep in sync with each peer: iterate over `NutPunch_GetPeers()` for `NutPunch_GetPeerCount()` entries, and use their address/port combo to send/receive datagram updates. In terms of winsock, it means calling `recvfrom()`/`sendto()` and passing the socket you got from `NutPunch_Done()`, as well as the remote peer's `sockaddr` struct. Refer to [the example code](src/test.c) for more info on how to construct it from a `NutPunch_GetPeers()` entry. Use `NutPunch_LocalPeer()` to get the local peer's index in the array as to avoid sending to and receiving from a bogus IP address.

[^1]: <https://learn.microsoft.com/en-us/windows/win32/winsock/using-winsock>
[^2]: <https://pastebin.com/JkGnQyPX>

## Premade Integrations

Not documented yet.

> **TODO**
> Add a [GekkoNet](https://github.com/HeatXD/GekkoNet) network adapter implementation. In the meantime, you can look into [the code of a real usecase](https://github.com/toggins/Klawiatura/blob/master/src/K_net.c).

## Public Instance

If you don't feel like [hosting your own instance](#hosting-a-nutpuncher-server), you can use our public instance as a kludge. In C code, write:

```c
NutPunch_SetServerAddr("95.163.233.200");
NutPunch_Join("lobby-id");
```

Or if you just want to try the test binary after building it:

```bat
@echo off
start .\build\nutpunchTest.exe 2 95.163.233.200
```

## Advanced Usage

You can change the `memcmp`, `memset`, and `memcpy` functions used in the implementation by redefining `NutPunch_Memcmp`, `NutPunch_Memset`, and `Nutpunch_Memcpy` respectively. For SDL3:

```c
#include <SDL3/SDL_stdinc.h>
#define NutPunch_Memcmp SDL_memcmp
#define NutPunch_Memset SDL_memset
#define NutPunch_Memcpy SDL_memcpy

#define NUTPUNCH_IMPLEMENTATION
#include "nutpunch.h" // IWYU pragma: keep
```

## Hosting a Nutpuncher Server

If you're dissatisfied with [the public instance](#public-instance), whether from needing to stick to a specific build or fork or whatever, you can host your own. Make sure to read [the introductory pamphlet](#introductory-lecture) before attempting this.

On Windows, use [the provided server binary](https://github.com/Schwungus/nutpunch/releases/tag/stable) and make sure the **UDP port `30001`** is open to the public.

On Linux, **if your system supports Wine**, you can use [our Docker image](https://github.com/Schwungus/nutpunch/pkgs/container/nutpuncher), e.g. with docker-compose:

```yaml
services:
  main:
    image: ghcr.io/schwungus/nutpuncher
    container_name: nutpuncher
    network_mode: host # kludge, required: just forwarding port 30001 breaks nutpuncher connectivity for unknown reasons
    restart: always
    tty: true # kludge, required: the Windows binary doesn't run at all without this
```

## Mental Notes

1. The error code `WSAEMSGSIZE` (`10040`) is fixed by getting a message buffer of at least `NUTPUNCH_PAYLOAD_SIZE` bytes. No idea why the puncher server keeps sending these payloads.
