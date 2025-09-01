<!-- markdownlint-disable MD033 MD045 -->

# NutPunch

<img align="right" src=".github/assets/nutpunch256.png">

> [!WARNING]
> Using NutPunch and UDP hole-punching makes sense **only if you're making a P2P networked game**. If you need server-based networking (**which is arguably much simpler to implement**), you're out of luck -- go elsewhere. You have been warned.

A UDP hole-punching library for REAL men (and women). Header-only. Brutal. Written in plain C.

## Introductory Lecture

This library implements P2P networking, where **each peer communicates with all others**. It's a complex model, and it could be counterproductive to use if you don't know what you're signing yourself up for. If you don't feel like reading the immediately following blanket of words and scribbles, you may skip to [using premade integrations](#premade-integrations).

Before you can punch any holes in your peers' NAT, you will need a hole-punching server **with a public IP address** assigned. Querying a public server lets us bust a gateway open to the global network, all while the server relays the connection info for other peers to us. If you're just testing, you can use [our public instance](#public-instance) instead of [hosting your own](#hosting-a-nutpuncher-server). The current server implementation uses a lobby-based approach, where each lobby supports up to 16 peers and is identified by a unique ASCII string.

In order to run your own hole-puncher server, you'll need to get the server binary from our [reference implementation releases](https://github.com/Schwungus/nutpunch/releases/tag/stable). [A Docker image](https://github.com/Schwungus/nutpunch/pkgs/container/nutpuncher) is also available for hosting a NutPuncher server on Linux. If you're in a pinch, don't have access to a public IP address, and your players reside on a LAN/virtual network such as [Radmin VPN](https://www.radmin-vpn.com), you can actually run NutPuncher locally and use your LAN IP address to connect to it.

Once you've figured out how the players are to connect to your hole-puncher server, you can start coding up your game. [The complete example](src/test.c) might be overwhelming at first, but make sure to skim through it before you do any heavy networking. Here's the general usage guide for the NutPunch library:

1. Join a lobby:
   1. Call `NutPunch_SetServerAddr("nutpuncher-ip-address")` to set the NutPuncher server address. You may either use an IPv4 address or the server's DNS hostname, if it has an `A` record set. Use the [public instance address](#public-instance) if you're having a severe brainfart reading all this.
   2. Call `NutPunch_Join("lobby-id")` immediately after `NutPunch_SetServerAddr(...)` to initiate the joining process.
2. Optionally add metadata to the lobby:
   1. If you join an empty lobby, you will be considered its master. A lobby master has the authority from the server to set global metadata for the lobby. This is usually needed to start games with specific parameters or to enforce an expected player count in a lobby. If you don't need metadata, you can just skip this entire step.
   2. After calling `NutPunch_Join()`, you can set metadata in the lobby by calling `NutPunch_Set()` as a master. A lobby can hold up to 16 fields of metadata, each of which are identified with 8-byte strings and can hold up to 32 bytes of data. Non-masters aren't allowed to set metadata, so that function becomes no-op for them. The actual metadata also won't be updated until the next call to `NutPunch_Update()`, which will be covered later.
3. Listen for hole-puncher server responses:
   1. Call `NutPunch_Update()` each frame, regardless of whether you're still joining or already playing with the boys. This will also automatically update lobby metadata back and forth.
   2. Check your status by matching the returned value against `NP_Status_*` constants. `NP_Status_Online` is the only one you need to handle explicitly, as you can safely start retrieving metadata and player count with it. Optionally, you can also handle `NP_Status_Error` and get clues as to what's wrong by calling `NutPunch_GetLastError()`.
4. Optionally read metadata from the lobby during `NP_Status_Online` status. Use `NutPunch_Get()` to get a pointer to a metadata field, which you can then read from (as long as it's valid and is the exact size that you expect it to be). These pointers are volatile, especially when calling `NutPunch_Update()`, so if you need to use the gotten value more than once, cache it somewhere.
5. If all went well (i.e. you have enough metadata and player count is fulfilled), start your match.
6. Run the game logic.
7. Keep in sync with each peer: Send datagrams through `NutPunch_Send()` and poll for incoming datagrams by looping with `NutPunch_HasNext()` and retrieving with `NutPunch_NextPacket()`. In scenarios where you need to hold packet data in a static `char` array, set the array size to `NUTPUNCH_BUFFER_SIZE`[^kb].
8. Come back to step 6 the next frame. You're all Gucci!

[^kb]: `NUTPUNCH_BUFFER_SIZE` is currently `512000` as in 512 KB, which is the hard limit for the size of a single NutPunch packet. If you think this is a bit overkill, feel free to scold one of the developers on Discord.

## Premade Integrations

Not documented yet.

> **TODO**: Add a [GekkoNet](https://github.com/HeatXD/GekkoNet) network adapter implementation. In the meantime, you can look into [the code of a real usecase](https://github.com/toggins/Klawiatura/blob/master/src/K_net.c).

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

You can specify custom memory handling functions for NutPunch to use through `#define`s. For example, if you're working with SDL3:

```c
#include <SDL3/SDL_stdinc.h>

#define NUTPUNCH_IMPLEMENTATION
#define NutPunch_Memcmp SDL_memcmp
#define NutPunch_Memset SDL_memset
#define NutPunch_Memcpy SDL_memcpy
#define NutPunch_Malloc SDL_malloc
#define NutPunch_Free SDL_free
#include <nutpunch.h>
```

## Hosting a NutPuncher Server

If you're dissatisfied with [the public instance](#public-instance), whether from needing to stick to a specific build or fork or whatever, you can host your own. Make sure to read [the introductory pamphlet](#introductory-lecture) before attempting this.

On Windows and Linux, use [the provided server binary](https://github.com/Schwungus/nutpunch/releases/tag/stable) and make sure the **UDP port `30001`** is open to the public.

On Linux, you may also use [our Docker image](https://github.com/Schwungus/nutpunch/pkgs/container/nutpuncher), e.g. with docker-compose:

```yaml
services:
  main:
    image: ghcr.io/schwungus/nutpuncher
    container_name: nutpuncher
    ports: [30001:30001/udp]
    restart: always
```

If you're on MacOS, well, bad luck, buddy...
