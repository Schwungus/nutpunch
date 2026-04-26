<!-- markdownlint-disable MD033 MD045 -->

# NutPunch

<img align="right" src=".github/assets/nutpunch256.png">

> [!CAUTION]
> NutPunch implements **UDP-based peer-to-peer networking**. Use only if you know what you're getting yourself into. **Client-server architecture** is a lot more commonplace in games, and arguably much easier to implement and understand. You have been warned. Think for yourself to make the right decision.

NutPunch is a UDP hole-punching suite for REAL men (and women). [ENet](https://github.com/lsalzman/enet)-powered. Written in plain C. Brutal as hell.

Comes with a [public instance](#public-instance) for out-of-the-box integration.

:heavy_check_mark: [Schwungus](https://github.com/Schwungus)-certified.

## Troubleshooting

If you're having **connectivity issues in a game powered by NutPunch**, please make sure **there is a direct route to your computer** from the public network. That means, even if you're on a hotel Wi-Fi network in rural Turkmenistan, NutPunch should still work, as long as you **aren't masking your outbound IP address system-wide**. Using a proxy service for accessing the Web shouldn't interfere as long as **you aren't routing your whole traffic in TUN mode** or something similar.

Here's a short infographic for troubleshooting connectivity with [v2rayN](https://github.com/2dust/v2rayN) and similar proxy clients:

![An infographic telling you to disable TUN mode.](.github/assets/infographic.png)

## Introductory Lecture

This library implements P2P networking, where **each peer communicates with all others**. It's a complex model, and it could be counterproductive to use if you don't know what you're signing yourself up for. If you don't feel like reading the immediately following blanket of words and scribbles, you may skip to [using premade integrations](#premade-integrations).

Before you can punch any holes in your peers' NAT, you will need a hole-punching server **with a public IP address** assigned. Querying a public server lets us bust a gateway open to the global network, all while the server relays the connection info for other peers to us. If you're just testing, you can use [our public instance](#public-instance) instead of [hosting your own](#hosting-your-own-nutpuncher). The current server implementation uses a lobby-based approach, where each lobby supports up to 16 peers and is identified by a unique ASCII string.

In order to run your own hole-puncher server, you'll need to get the server binary from our [reference implementation releases](https://github.com/Schwungus/nutpunch/releases/tag/rolling). If you're in a pinch, don't have access to a public IP address, and your players reside on a LAN/virtual network such as [Radmin VPN](https://www.radmin-vpn.com), you can actually run NutPuncher locally and use your LAN IP address to connect to it.

Once you've figured out how the players are to connect to your hole-puncher server, you can start coding up your game. [The complete example](src/Test.c) might be overwhelming at first, but make sure to skim through it before you do any heavy networking. Here's the general usage guide for the NutPunch library:

1. Host a lobby with `NutPunch_Host("lobby-id")`, or join an existing one with `NutPunch_Join("lobby-id")`.
2. Listen for events:
   1. Call `NutPunch_Update()` each frame, regardless of whether you're still joining or already [playing with the boys](https://nonk.dev/blog/battlecry). This will also automatically send lobby metadata back and forth to the NutPuncher server.
   2. Check your status by matching the returned value against `NPS_*` constants. `NPS_Online` is what you're looking for normally, but make sure to handle `NPS_Error`. To get a human-readable error description, call `NutPunch_GetLastError()`.
3. Run the game logic.
4. Keep in sync with the peers:
   1. Send messages with `NutPunch_Send()`.
   2. Poll for incoming messages with `NutPunch_HasMessage()` and retrieve them with `NutPunch_NextMessage()`.
   3. Set/retrieve lobby or peer metadata with `NutPunch_Set*Data()`/`NutPunch_Get*Data()`.
5. Repeat steps 2 through 4. You're all Gucci!

An important aspect of NutPunch networking is the ability to set peer/lobby metadata in a simplified key-value-store fashion. Peer metadata can include e.g. the peer's nickname, their skin spritesheet name, their lives count - anything you can squeeze into a 16-byte null-terminated string, mapped to 8-byte null-terminated string key. The same applies to lobby metadata: this could be the name of the map you're playing, a seed value to generate the map procedurally, the difficulty level, etc.

Call `NutPunch_Set*Data(...)`/`NutPunch_Get*Data(...)` to set/get key-value pairs; replace the asterisk with either `Peer` or `Lobby`. Setting metadata only does anything if you're "in charge" of the metadata object: either you're the lobby's master and want to set the lobby's metadata, or you're trying to set your own metadata as a peer.

## Premade Integrations

None yet.

> **TODO**: Add a [GekkoNet](https://github.com/HeatXD/GekkoNet) network adapter implementation. In the meantime, you can look into [the code of a real usecase](https://github.com/toggins/Klawiatura/blob/master/src/K_net.c).

## Installation

If you're using CMake, you can include this library in your project by adding the following to your `CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(NutPunch
    GIT_REPOSITORY https://github.com/Schwungus/nutpunch.git
    GIT_TAG stable) # you can use a specific commit hash here
FetchContent_MakeAvailable(NutPunch)

add_executable(MyGame main.c) # your game's CMake target goes here
target_link_libraries(MyGame PRIVATE NutPunch)
```

For other build systems (or lack thereof), follow the procedure described in our [`CMakeLists.txt`](CMakeLists.txt):

1. Build ENet from source.
2. Build `src/NutPunch.c` with `include` set as its include-directory.
3. Link your game against NutPunch, ENet, and (if you're a Windose user) `winpthread`.

## Basic Usage

Once you've built & linked against NutPunch, just include [`<NutPunch.h>`](include/NutPunch.h) and get cracking on some netcode. Here's a really basic example:

```c
#include <stdlib.h> // for EXIT_SUCCESS

#include <NutPunch.h>

int main(int argc, char* argv[]) {
    (void)argc, (void)argv;

    NutPunch_Join("MyLobby");

    for (;;) { // your game's mainloop goes here...
        NutPunch_Update();
        Sleep(1000 / 60);
    }

    return EXIT_SUCCESS;
}
```

If you want to see all the juicy APIs in action, read up on [`Test.c`](src/Test.c) from this repo. For a general overview of available functionality, just read the doc-comments for the functions around the middle of [`NutPunch.h`](include/NutPunch.h). Also take a look at [the advanced usage section](#advanced-usage) to discover things you can customize.

## Public Instance

If you don't feel like [hosting your own instance](#hosting-your-own-nutpuncher), you may use our public instance. It's used by default unless a different server is specified.

If you want to be explicit about using the public instance, call `NutPunch_SetServerAddr`:

```c
NutPunch_SetServerAddr(NUTPUNCH_DEFAULT_SERVER);
NutPunch_Join("lobby-id");
```

## Advanced Usage

### Customize Logger Implementation

You can override NutPunch's logging facility by setting `NP_Logger`. Here's a simple example of logging to `stderr` rather than `stdout` as done by default:

```c
#include <NutPunch.h>

static void my_logger(const char* fmt, ...) {
    va_list args = {0};
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fflush(stderr);
}

int main(int argc, char* argv[]) {
    (void)argc, (void)argv;
    NP_Logger = my_logger;
    // use NutPunch...
}
```

## Hosting your own NutPuncher

If you're dissatisfied with [the public instance](#public-instance), whether from needing to stick to a specific build or fork or whatever, you can host your own. Make sure to read [the introductory pamphlet](#introductory-lecture) before attempting this.

**TODO**: document how to build a NutPuncher yourself.
