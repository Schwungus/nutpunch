// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.
//
// In jurisdictions that recognize copyright laws, the author or authors
// of this software dedicate any and all copyright interest in the
// software to the public domain. We make this dedication for the benefit
// of the public at large and to the detriment of our heirs and
// successors. We intend this dedication to be an overt act of
// relinquishment in perpetuity of all present and future rights to this
// software under copyright law.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//
// For more information, please refer to <https://unlicense.org>

#ifndef NUTPUNCH_H
#define NUTPUNCH_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(_WIN64)

#define NUTPUNCH_WINDOSE

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#endif

/// The default NutPuncher instance. It's public, so feel free to [ab]use it.
#define NUTPUNCH_DEFAULT_SERVER "nutpunch.schwung.us"

/// Maximum amount of players in a lobby. Not intended to be customizable.
#define NUTPUNCH_MAX_PLAYERS (8)

/// Increment this every time you break the communications format between the peer and the
/// NutPuncher, to make it use a different port and retain compatibility with the previous versions
/// by keeping the old NutPunchers running.
#define NUTPUNCH_API_VERSION (2)

/// The UDP port used by the nutpunching mediator server.
#define NUTPUNCH_SERVER_PORT (30000 + NUTPUNCH_API_VERSION)

#ifndef NUTPUNCH_SERVER_TIMEOUT_INTERVAL
/// How many milliseconds to wait for NutPuncher to respond before disconnecting.
#define NUTPUNCH_SERVER_TIMEOUT_INTERVAL ((NutPunch_Clock)5000)
#endif

#ifndef NUTPUNCH_PEER_TIMEOUT_INTERVAL
/// How many milliseconds to wait for a peer to respond before timing out.
#define NUTPUNCH_PEER_TIMEOUT_INTERVAL ((NutPunch_Clock)5000)
#endif

/// How many bytes to reserve for every network packet.
#define NUTPUNCH_BUFFER_SIZE (1024)

/// The maximum amount of results `NutPunch_LobbyList` can provide.
#define NUTPUNCH_MAX_SEARCH_RESULTS (16)

/// The maximum amount of filters you can pass to `NutPunch_FindLobbies`.
#define NUTPUNCH_MAX_SEARCH_FILTERS (8)

/// Maximum length of a metadata field name.
#define NUTPUNCH_FIELD_NAME_MAX (8)

/// Maximum volume of data you can store in a metadata field.
#define NUTPUNCH_FIELD_DATA_MAX (16)

/// Maximum amount of metadata fields per lobby/player.
#define NUTPUNCH_MAX_FIELDS (8)

/// How many milliseconds to wait before resending a reliable packet.
#define NUTPUNCH_BOUNCE_INTERVAL ((NutPunch_Clock)250)

#define NUTPUNCH_CHANNEL_COUNT (1 << (8 * sizeof(NutPunch_Channel)))

#ifndef NUTPUNCH_NOSTD
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

// we still depend on `time.h` through `NutPunch_TimeNS()` and a few others...
#include <time.h>

/// The internal unique identifier for your peer. You don't actually interact with it in your code.
typedef char NutPunch_PeerId[8];

/// A string uniquely identifying a NutPunch lobby.
typedef char NutPunch_LobbyId[16];

/// A magic string used for matchmaking.
typedef char NutPunch_QueueId[16];

typedef uint8_t NutPunch_Channel, NutPunch_Peer;

/// A singular entry of lobby/peer metadata.
typedef struct {
    char name[NUTPUNCH_FIELD_NAME_MAX];
    char data[NUTPUNCH_FIELD_DATA_MAX];
    uint8_t size;
} NutPunch_Field;

/// An array of key-value metadata pairs.
typedef NutPunch_Field NutPunch_Metadata[NUTPUNCH_MAX_FIELDS];

/// A struct containing a comparison between previous and updated metadata.
typedef struct {
    NutPunch_Field then, now;
} NutPunch_FieldDiff;

/// Same as `NutPunch_FieldDiff`, but with a peer identifier.
typedef struct {
    NutPunch_Field then, now;
    NutPunch_Peer peer;
} NutPunch_PeerFieldDiff;

/// Special fields you can query inside `NutPunch_Filter`s.
typedef enum {
    /// Lobby player count.
    NPSF_Players = 1,
    /// Lobby max players.
    NPSF_Capacity,
} NutPunch_SpecialField;

/// A filter for use with `NutPunch_FindLobbies`.
typedef struct {
    union {
        struct {
            uint8_t alwayszero;
            char name[NUTPUNCH_FIELD_NAME_MAX];
            char value[NUTPUNCH_FIELD_DATA_MAX];
        } field;
        struct {
            uint8_t index;
            int8_t value;
        } special;
    };
    uint8_t comparison;
} NutPunch_Filter;

/// About as much data as you are allowed to see from lobbies you aren't part of.
typedef struct {
    NutPunch_LobbyId name;
    uint8_t players, capacity;
} NutPunch_LobbyInfo;

/// A list of lobbies returned by the NutPuncher.
typedef struct {
    uint8_t count;
    const NutPunch_LobbyInfo* lobbies;
} NutPunch_LobbyList;

/// A snapshot of a lobby's metadata returned by the nutpuncher.
typedef struct {
    NutPunch_LobbyId lobby;
    uint8_t count;
    const NutPunch_Field* metadata;
} NutPunch_LobbyMetadata;

/// Comparison operators used in `NutPunch_Filter`s.
typedef enum {
    NPF_Not = 1 << 0,
    NPF_Eq = 1 << 1,
    NPF_Less = 1 << 2,
    NPF_Greater = 1 << 3,
} NutPunch_Operator;

/// Describes the result of calling `NutPunch_Update`.
typedef enum {
    /// An error occurred. Retrieve an error message with `NutPunch_GetLastError()`.
    NPS_Error,
    /// Ready to join but not in a lobby yet.
    NPS_Idle,
    /// Actively participating in a lobby.
    NPS_Online,
} NutPunch_UpdateStatus;

typedef enum {
    NPE_Ok,
    NPE_Sybau,
    NPE_NoSuchLobby,
    NPE_LobbyExists,
    NPE_LobbyFull,
    NPE_QueueNoMatch,
    NPE_Max,
} NutPunch_ErrorCode;

typedef enum {
    /// Callback data: the index of the new peer. Their metadata is available for reading.
    NPCB_PeerJoined,
    /// Callback data: the index of the leaving peer. Their metadata is still available for reading.
    NPCB_PeerLeft,
    /// Callback data: the index of the lobby's new master.
    NPCB_NewMaster,
    /// Callback data: see `NutPunch_FieldChanged`.
    NPCB_LobbyMetadataChanged,
    /// Callback data: see `NutPunch_PeerFieldChanged`.
    NPCB_PeerMetadataChanged,
    /// Callback data: see `NutPunch_LobbyList`.
    NPCB_LobbyList,
    /// Callback data: see `NutPunch_LobbyMetadata`.
    NPCB_LobbyMetadata,
    /// Callback data: the lobby ID generated by queuing.
    NPCB_QueueCompleted,
    /// Total callback-event count. Do not pass this to `NutPunch_Register`.
    NPCB_Count,
} NutPunch_CallbackEvent;

typedef void (*NutPunch_Callback)(const void*);

/// Set a custom NutPuncher server address.
void NutPunch_SetServerAddr(const char* hostname);

/// Connect to NutPuncher without joining a lobby, mainly for lobby queries. Return `false` if a
/// network error occurs and `true` otherwise.
bool NutPunch_Query();

/// Join a lobby by its ID. Return `false` if a network error occurs and `true` otherwise.
///
/// If no lobby exists with this ID, an error status spits out of `NutPunch_Update()` rather than
/// immediately here.
bool NutPunch_Join(const char* lobby_id);

/// Host a lobby with the specified ID. Return `false` if a network error
/// occurs and `true` otherwise.
///
/// If the lobby with the same ID exists, an error status spits out of `NutPunch_Update()` rather
/// than immediately here.
bool NutPunch_Host(const char* lobby_id);

/// Join the queue for matchmaking. The queue ID is used to find similar peers. Return `false` if a
/// network error occurs and `true` otherwise.
bool NutPunch_Grindr(const char* queue_id);

/// Unlist the lobby after calling `NutPunch_Host`.
void NutPunch_SetUnlisted(bool);

/// Return `true` if the current lobby is unlisted.
bool NutPunch_IsUnlisted();

/// Change the maximum player count after calling `NutPunch_Host`.
void NutPunch_SetMaxPlayers(int players);

/// Get the maximum player count of the lobby you are in. Returns 0 if you aren't in a lobby.
int NutPunch_GetMaxPlayers();

/// Register an event handler. The callback functions are called as part of `NutPunch_Update()`.
void NutPunch_Register(NutPunch_CallbackEvent event, NutPunch_Callback cb);

/// Call this at the end of your program to disconnect gracefully and run other semi-important
/// cleanup routines.
void NutPunch_Cleanup();

/// Call this every frame to update nutpunch. Returns one of the `NPS_*` constants you need to match
/// against to see if something goes wrong.
NutPunch_UpdateStatus NutPunch_Update();

/// Sends all queued outgoing packets early (before a `NutPunch_Update()`). Useful in niche cases.
void NutPunch_Flush();

/// Requests metadata from a lobby. If you aren't connected to the NutPuncher, call
/// `NutPunch_Query()` first.
/// Triggers a `NPCB_LobbyMetadata` callback if successful.
void NutPunch_RequestLobbyData(const NutPunch_LobbyId lobby);

/// Returns metadata from a lobby.
///
/// See `NUTPUNCH_FIELD_NAME_MAX` and `NUTPUNCH_FIELD_DATA_MAX` for the amount of data you can
/// squeeze into a field.
const void* NutPunch_GetLobbyData(const char* name, int* size);

/// Returns metadata from a peer.
///
/// See `NUTPUNCH_FIELD_NAME_MAX` and `NUTPUNCH_FIELD_DATA_MAX` for the amount of data you can
/// squeeze into a field.
const void* NutPunch_GetPeerData(NutPunch_Peer peer, const char* name, int* size);

/// Sets metadata in the lobby. Doesn't do anything if you're not the master of the lobby.
///
/// See `NUTPUNCH_FIELD_NAME_MAX` and `NUTPUNCH_FIELD_DATA_MAX` for the amount of data you can
/// squeeze into a field.
void NutPunch_SetLobbyData(const char* name, int size, const void* data);

/// Sets metadata for the local peer.
///
/// See `NUTPUNCH_FIELD_NAME_MAX` and `NUTPUNCH_FIELD_DATA_MAX` for the amount of data you can
/// squeeze into a field.
void NutPunch_SetPeerData(const char* name, int size, const void* data);

/// Set the maximum amount of channels this peer can receive from. Packets with an index higher than
/// or equal to maximum channel count are discarded silently.
///
/// The default behavior is receiving only on channel 0.
///
/// Passing less than 1 or more than 256 channels fails silently.
///
/// Solves the weaponization issue (#32).
void NutPunch_SetChannelCount(int);

/// Check if there is a packet waiting in the receiving queue for the specified channel index.
/// Retrieve message data by calling `NutPunch_NextMessage(channel)`, which see.
bool NutPunch_HasMessage(NutPunch_Channel);

/// Retrieves the next packet in the receiving queue for the specified channel index. Reads up to
/// `size` bytes into `out`. Returns the index of the peer who sent it.
///
/// In case of an error, logs it and returns `NUTPUNCH_MAX_PLAYERS`. You can retrieve a
/// human-readable message later with `NutPunch_GetLastError()`.
///
/// `size` must be set to the output buffer's size. Passing an output buffer that is smaller than
/// `size` will crash your entire program.
int NutPunch_NextMessage(NutPunch_Channel, void* out, int* size);

/// Send data on the specified channel to the specified peer. Copies the data into a dynamically
/// allocated buffer of `size` bytes.
///
/// For reliable packet delivery, try `NutPunch_SendReliably`.
void NutPunch_Send(NutPunch_Channel, NutPunch_Peer, const void* data, int size);

/// Send data on the specified channel to the specified peer, expecting them to acknowledge the fact
/// of reception. Functions similarly to `NutPunch_Send` otherwise, which see.
void NutPunch_SendReliably(NutPunch_Channel, NutPunch_Peer, const void* data, int size);

/// Count how many "live" peers we have a route to, including our local peer.
///
/// Do not use this as an upper bound for iterating over peers. Iterate from 0 to
/// `NUTPUNCH_MAX_PLAYERS` and check each peer individually with `NutPunch_PeerAlive`.
int NutPunch_PeerCount();

/// Return `true` if you are connected to the peer with the specified index.
///
/// Use `NUTPUNCH_MAX_PLAYERS` as the upper bound for iterating, and check each peer's status
/// individually using this function.
bool NutPunch_PeerAlive(NutPunch_Peer);

/// Get the local peer's index. Available only after successfully joining a lobby. Returns
/// `NUTPUNCH_MAX_PLAYERS` if this fails for any reason.
int NutPunch_LocalPeer();

/// Get the master peer's index. Available only after successfully joining a lobby. Returns
/// `NUTPUNCH_MAX_PLAYERS` if this fails for any reason.
int NutPunch_MasterPeer();

/// Returns `true` if we are in a lobby. This doesn't guarantee local/master peer indices or any
/// metadata is available; for that, see `NutPunch_IsReady()`.
bool NutPunch_IsOnline();

/// Returns `true` if we are in a lobby AND are ready to accept P2P connections. Lobby metadata and
/// local/master peer indices will be available by this point.
bool NutPunch_IsReady();

/// Call this to gracefully disconnect from a lobby.
void NutPunch_Disconnect();

/// Search for lobbies. If you aren't connected to the NutPuncher, call `NutPunch_Query()` first.
///
/// The list of lobbies will be retrieved through the `NutPunch_LobbyList` callback.
///
/// You can optionally pass an array of filters. Each filter consists of either a special or a named
/// metadata field, with a corresponding value to compare it to. All filters must match in order for
/// a lobby to be listed.
///
/// Bitwise-or the `NPF_*` constants to set a filter's comparison flags.
///
/// To query "special" fields such as lobby current or max player count, pass one of the `NPSF_*`
/// constants with an `int8_t` value to compare against. For example, to query lobbies for exactly a
/// 2-player duo to play:
///
/// ```c
/// NutPunch_Filter filters[2] = {0};
///
/// // exactly 2 players capacity:
/// filters[0].special.index = NPSF_Capacity;
/// filters[0].special.value = 2;
/// filters[0].comparison = NPF_Eq;
///
/// // less than two players in the lobby so we can join:
/// filters[1].special.index = NPSF_Players;
/// filters[1].special.value = 2;
/// filters[1].comparison = NPF_Less;
///
/// NutPunch_FindLobbies(2, &filters);
/// ```
///
/// To query metadata fields, `memcpy` their names and values into the filter's `field` property.
/// The comparison will be performed bytewise in a fashion similar to `memcmp`.
///
/// Request and retrieve lobby metadata by calling `NutPunch_LobbyMetadata()` with a lobby
/// identifier.
void NutPunch_FindLobbies(int filter_count, const NutPunch_Filter* filters);

/// Call this to reset the underlying socket in case of an inexplicable error.
void NutPunch_Reset();

/// Get the human-readable description of the latest error that occurred in `NutPunch_Update()` or
/// several other internal functions.
const char* NutPunch_GetLastError();

/// Return a substring of `path` without its directory name. A utility function used internally in
/// the default implementation of `NutPunch_Log`.
const char* NutPunch_Basename(const char* path);

#define NUTPUNCH_SEC ((NutPunch_Clock)1000000000)
#define NUTPUNCH_MS (NUTPUNCH_SEC / ((NutPunch_Clock)1000))

typedef uint64_t NutPunch_Clock;
NutPunch_Clock NutPunch_TimeNS();

#ifndef NutPunch_Log
#include <stdio.h>
#define NutPunch_Log(msg, ...)                                                                     \
    do {                                                                                           \
        fprintf(                                                                                   \
            stdout, "(%s:%d) " msg "\n", NutPunch_Basename(__FILE__), __LINE__, ##__VA_ARGS__);    \
        fflush(stdout);                                                                            \
    } while (0)
#endif

#ifdef NUTPUNCH_IMPLEMENTATION

#ifdef NUTPUNCH_WINDOSE

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET NP_Socket;
#define NUTPUNCH_INVALID_SOCKET INVALID_SOCKET

#define NP_SockError() WSAGetLastError()
#define NP_WouldBlock WSAEWOULDBLOCK
#define NP_ConnReset WSAECONNRESET
#define NP_TooFat WSAEMSGSIZE

#else

// everything non-winsoque comes from <https://stackoverflow.com/a/28031039>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <netdb.h>

#include <errno.h>
#include <fcntl.h>

typedef int64_t NP_Socket;
#define NUTPUNCH_INVALID_SOCKET (-1)

#define NP_SockError() errno
#define NP_WouldBlock EWOULDBLOCK
#define NP_ConnReset ECONNRESET
#define NP_TooFat EMSGSIZE

#endif

#ifndef NutPunch_SNPrintF
#include <stdio.h>
#define NutPunch_SNPrintF snprintf
#endif

#if !defined(NutPunch_Malloc) && !defined(NutPunch_Free)

#include <stdlib.h>
#define NutPunch_Malloc malloc
#define NutPunch_Free free

#elif !defined(NutPunch_Malloc) || !defined(NutPunch_Free)

#error Define NutPunch_Malloc and NutPunch_Free together!

#endif // NutPunch_{Malloc,Free}

#if !defined(NutPunch_Memcpy) && !defined(NutPunch_Memset) && !defined(NutPunch_Memcmp)

#include <string.h>
#define NutPunch_Memcpy memcpy
#define NutPunch_Memset memset
#define NutPunch_Memcmp memcmp

#elif !defined(NutPunch_Memcpy) || !defined(NutPunch_Memset) || !defined(NutPunch_Memcmp)

#error Define NutPunch_Mem{cpy,set,cmp} together!

#endif // NutPunch_Mem*

#define NP_Memzero(array) NutPunch_Memset(array, 0, sizeof(array))
#define NP_MemzeroRef(ref) NutPunch_Memset(&(ref), 0, sizeof(ref))

#define NP_Info(...) NutPunch_Log("INFO: " __VA_ARGS__)
#define NP_Warn(...)                                                                               \
    do {                                                                                           \
        NutPunch_Log("WARN: " __VA_ARGS__);                                                        \
        NutPunch_SNPrintF(NP_LastError, sizeof(NP_LastError), ##__VA_ARGS__);                      \
    } while (0)

#ifdef NUTPUNCH_TRACING
#define NP_Trace(...) NutPunch_Log("TRACE: " __VA_ARGS__)
#else // clang-format off
#define NP_Trace(...) do {} while (0)
#endif // clang-format on

typedef uint8_t NP_NetMode;
enum {
    NPNM_Normal,
    NPNM_Query,
    NPNM_Matchmaking,
};

typedef uint32_t NP_PacketIndex;
typedef struct sockaddr_in NP_AddrInfo;
typedef uint8_t NP_HeartbeatFlagsStorage;

typedef uint8_t NP_Header[4];

typedef struct NP_Data {
    uint8_t* data;
    struct NP_Data* next;
    NP_PacketIndex index;
    uint32_t size;
    NutPunch_Peer destination;
    NutPunch_Clock sent_at;
    uint8_t f_delete;
} NP_Data;

typedef struct {
    NP_AddrInfo addr;
    NutPunch_Clock last_ping, first_ping;

    NP_PacketIndex recv_counter, send_counter;
    NP_Data* out_of_order[NUTPUNCH_CHANNEL_COUNT];

    NutPunch_Metadata metadata;
} NP_PeerInfo;

typedef struct {
    NP_AddrInfo addr;
    int size;
    const uint8_t* data;
} NP_Message;

// tightly packed structs matching packet layouts.
#pragma pack(push, 1)

typedef struct {
    uint8_t ip[4];
    uint16_t port;
} NP_PeerAddr;

typedef struct {
    uint8_t unlisted;
    NutPunch_Peer local, master, capacity;
    NP_PeerAddr peers[2][NUTPUNCH_MAX_PLAYERS];
    NutPunch_Metadata metadata;
} NP_Beating;

typedef struct {
    NutPunch_Channel channel;
    NP_PacketIndex packet;
} NP_Acky;

typedef struct {
    NutPunch_PeerId peer;
    NutPunch_LobbyId lobby;
    NP_HeartbeatFlagsStorage flags;
    NP_PeerAddr internal_addr;
    NutPunch_Metadata lobby_metadata;
} NP_Heartbeat;

#pragma pack(pop)

typedef struct {
    const char identifier[sizeof(NP_Header) + 1];
    const int16_t packet_size;
    void (*const handler)(NP_Message);
} NP_MessageType;

#define NP_ANY_LEN (-1)
#define NP_PING_SIZE (sizeof(NP_Header) + 1 + sizeof(NutPunch_Metadata))

static void NP_HandlePing(NP_Message), NP_HandleGTFO(NP_Message), NP_HandleBeating(NP_Message),
    NP_HandleListing(NP_Message), NP_HandleLobbyMetadata(NP_Message), NP_HandleAcky(NP_Message),
    NP_HandleData(NP_Message), NP_HandlePong(NP_Message), NP_HandleDate(NP_Message);

// clang-format off
static const NP_MessageType NP_MessageTypes[] = {
	{"PING", 1 + sizeof(NutPunch_Metadata), NP_HandlePing         },
	{"LIST", NP_ANY_LEN,                    NP_HandleListing      },
	{"LGMA", NP_ANY_LEN,                    NP_HandleLobbyMetadata},
	{"ACKY", sizeof(NP_Acky),               NP_HandleAcky         },
	{"DATA", NP_ANY_LEN,                    NP_HandleData         },
	{"GTFO", 1,		                        NP_HandleGTFO         },
	{"BEAT", sizeof(NP_Beating),            NP_HandleBeating      },
    {"PONG", 0,		                        NP_HandlePong         },
    {"DATE", sizeof(NutPunch_LobbyId),      NP_HandleDate         },
};
// clang-format on

static char NP_LastError[512] = "";
static NutPunch_Clock NP_LastBeating = 0;

static bool NP_InitDone = false, NP_Closing = false;
static NutPunch_UpdateStatus NP_LastStatus = NPS_Idle;

static char NP_LobbyId[sizeof(NutPunch_LobbyId) + 1] = {0};
static NutPunch_PeerId NP_PeerId = {0};
static NutPunch_QueueId NP_QueueId = {0};

static NP_PeerInfo NP_Peers[NUTPUNCH_MAX_PLAYERS] = {0};
static NutPunch_Peer NP_LocalPeer = NUTPUNCH_MAX_PLAYERS, NP_Master = NUTPUNCH_MAX_PLAYERS,
                     NP_MaxPlayers = 0;

static NutPunch_Callback NP_Callbacks[NPCB_Count] = {0};

static NP_Socket NP_Sock = NUTPUNCH_INVALID_SOCKET;
static NP_AddrInfo NP_PuncherAddr = {0};
static char NP_ServerHost[128] = {0};

static NutPunch_Channel NP_MaxChannel = 0;
static NP_Data* NP_QueueOut[NUTPUNCH_CHANNEL_COUNT] = {0};
static NP_Data* NP_QueueIn[NUTPUNCH_CHANNEL_COUNT] = {0};

static bool NP_Unlisted = false;
static NutPunch_Metadata NP_LobbyMetadata = {0}, NP_PeerMetadata = {0};

static NP_NetMode NP_Mode = NPNM_Normal;

static NP_HeartbeatFlagsStorage NP_HeartbeatFlags = 0;
enum {
    NP_HB_JoinExisting = 1 << 0,
    NP_HB_Unlisted = 1 << 1,
    NP_HB_Grindr = 1 << 2,
};

static int NP_SendDirectly(NP_AddrInfo, const void*, int);

static uint16_t* NP_AddrFamily(NP_AddrInfo* addr) {
    return (uint16_t*)&addr->sin_family;
}

static uint32_t* NP_AddrRaw(NP_AddrInfo* addr) {
    return (uint32_t*)&addr->sin_addr.s_addr;
}

static uint16_t* NP_AddrPort(NP_AddrInfo* addr) {
    return &addr->sin_port;
}

static bool NP_AddrEq(NP_AddrInfo a, NP_AddrInfo b) {
    return !NutPunch_Memcmp(NP_AddrRaw(&a), NP_AddrRaw(&b), 4)
           && !NutPunch_Memcmp(NP_AddrPort(&a), NP_AddrPort(&b), 2);
}

static bool NP_AddrNull(NP_AddrInfo addr) {
    return !*NP_AddrPort(&addr) && !*NP_AddrRaw(&addr);
}

static void NP_CleanupPackets(NP_Data** queue) {
    while (*queue) {
        NP_Data* const ptr = *queue;
        *queue = ptr->next;
        NutPunch_Free(ptr->data);
        NutPunch_Free(ptr);
    }
}

static void NP_NukeLobbyData() {
    NP_Mode = NPNM_Normal;
    NP_Closing = NP_Unlisted = false;
    NP_LocalPeer = NP_Master = NUTPUNCH_MAX_PLAYERS;
    NP_Memzero(NP_LobbyMetadata), NP_Memzero(NP_PeerMetadata);
    NP_Memzero(NP_Peers);

    for (int i = 0; i < NUTPUNCH_CHANNEL_COUNT; i++) {
        NP_CleanupPackets(&NP_QueueIn[i]);
        NP_CleanupPackets(&NP_QueueOut[i]);
    }
}

static void NP_NukeRemote() {
    NP_LobbyId[0] = 0, NP_HeartbeatFlags = 0;
    NP_MemzeroRef(NP_PuncherAddr), NP_Memzero(NP_Peers);
    NP_LastStatus = NPS_Idle;
}

static void NP_NukeSocket(NP_Socket* sock) {
    if (*sock == NUTPUNCH_INVALID_SOCKET)
        return;
#ifdef NUTPUNCH_WINDOSE
    shutdown(*sock, SD_BOTH);
    closesocket(*sock);
#else
    close(*sock);
#endif
    *sock = NUTPUNCH_INVALID_SOCKET;
}

static void NP_ResetImpl() {
    NP_LastBeating = NutPunch_TimeNS();
    NP_NukeRemote(), NP_NukeLobbyData();
    NP_NukeSocket(&NP_Sock);
}

static void NP_LazyInit() {
    if (NP_InitDone)
        return;
    NP_InitDone = true;

    srand(NutPunch_TimeNS()); // TODO: abstract `rand` & `srand`
    for (int i = 0; i < sizeof(NutPunch_PeerId); i++)
        NP_PeerId[i] = (char)('A' + rand() % 26);

#ifdef NUTPUNCH_WINDOSE
    WSADATA bitch = {0};
    WSAStartup(MAKEWORD(2, 2), &bitch);
#endif

    NP_ResetImpl();

    // clang-format off
	NutPunch_Log(".-------------------------------------------------------------.");
	NutPunch_Log("| For troubleshooting multiplayer connectivity, please visit: |");
	NutPunch_Log("|    https://github.com/Schwungus/nutpunch#troubleshooting    |");
	NutPunch_Log("'-------------------------------------------------------------'");
    // clang-format on
}

void NutPunch_Reset() {
    NP_LazyInit();
    NP_ResetImpl();
}

void NutPunch_SetServerAddr(const char* hostname) {
    if (!hostname)
        hostname = "";
    NutPunch_SNPrintF(NP_ServerHost, sizeof(NP_ServerHost), "%s", hostname);
}

static int NP_FieldNameSize(const char* name) {
    if (!name)
        return 0;
    for (int i = 0; i < NUTPUNCH_FIELD_NAME_MAX; i++)
        if (!name[i])
            return i;
    return NUTPUNCH_FIELD_NAME_MAX;
}

static NutPunch_Field* NP_GetPeerFields(NutPunch_Peer peer) {
    if (NutPunch_IsOnline() && peer == NutPunch_LocalPeer())
        return NP_PeerMetadata;
    else if (peer >= 0 && peer < NUTPUNCH_MAX_PLAYERS)
        return NP_Peers[peer].metadata;
    else
        return NULL;
}

static const void* NP_GetVar(const NutPunch_Field* fields, const char* name, int* size) {
    static uint8_t buf[NUTPUNCH_FIELD_DATA_MAX] = {0};
    NP_Memzero(buf);

    int name_size = NP_FieldNameSize(name);
    if (!name_size || !fields)
        goto none;

    for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
        const NutPunch_Field* const field = &fields[i];
        if (name_size != NP_FieldNameSize(field->name))
            continue;
        if (NutPunch_Memcmp(field->name, name, name_size))
            continue;
        if (field->size > sizeof(buf)) {
            NP_Warn("Metadata field size exceeds buffer size");
            goto none;
        }
        NutPunch_Memcpy(buf, field->data, field->size);
        if (size)
            *size = field->size;
        return buf;
    }

none:
    if (size)
        *size = 0;
    return NULL;
}

static void NP_SetVar(NutPunch_Field* fields, const char* name, int size, const void* data) {
    if (!fields)
        return;

    const int name_size = NP_FieldNameSize(name);
    if (!name_size)
        return;

    if (!data) {
        NP_Warn("No data?");
        return;
    }

    if (size < 1) {
        NP_Warn("Invalid metadata field size!");
        return;
    }

    if (size > NUTPUNCH_FIELD_DATA_MAX) {
        NP_Warn("Trimming metadata field from %d to %d bytes", size, NUTPUNCH_FIELD_DATA_MAX);
        size = NUTPUNCH_FIELD_DATA_MAX;
    }

    for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
        static const NutPunch_Field nullfield = {0};
        NutPunch_Field* const field = &fields[i];

        if (NutPunch_Memcmp(field, &nullfield, sizeof(nullfield))) {
            if (NP_FieldNameSize(field->name) != name_size)
                continue;
            if (NutPunch_Memcmp(field->name, name, name_size))
                continue;
        }

        NP_Memzero(field->name);
        NutPunch_Memcpy(field->name, name, name_size);

        NP_Memzero(field->data);
        NutPunch_Memcpy(field->data, data, size);

        field->size = size;
        return;
    }
}

void NutPunch_RequestLobbyData(const NutPunch_LobbyId lobby) {
    static uint8_t buf[sizeof(NP_Header) + sizeof(NutPunch_LobbyId)] = "LIST";
    NutPunch_Memcpy(buf + sizeof(NP_Header), lobby, sizeof(NutPunch_LobbyId));
    NP_SendDirectly(NP_PuncherAddr, buf, sizeof(buf));
}

const void* NutPunch_GetLobbyData(const char* name, int* size) {
    return NP_GetVar(NP_LobbyMetadata, name, size);
}

const void* NutPunch_GetPeerData(NutPunch_Peer peer, const char* name, int* size) {
    return NP_GetVar(NP_GetPeerFields(peer), name, size);
}

void NutPunch_SetLobbyData(const char* name, int size, const void* data) {
    NP_SetVar(NP_LobbyMetadata, name, size, data);
}

void NutPunch_SetPeerData(const char* name, int size, const void* data) {
    NP_SetVar(NP_PeerMetadata, name, size, data);
}

static bool NP_ResolveNutpuncher() {
    NP_LazyInit();

    struct addrinfo *resolved = NULL, hints = {0};
    hints.ai_family = AF_INET, hints.ai_socktype = SOCK_DGRAM, hints.ai_protocol = IPPROTO_UDP;

    if (!NP_ServerHost[0]) {
        NutPunch_SetServerAddr(NUTPUNCH_DEFAULT_SERVER);
        NP_Info("Connecting to the public NutPuncher as none was explicitly specified");
    }

    static char portfmt[8] = {0};
    NutPunch_SNPrintF(portfmt, sizeof(portfmt), "%d", NUTPUNCH_SERVER_PORT);

    if (getaddrinfo(NP_ServerHost, portfmt, &hints, &resolved)) {
        NP_Warn("NutPuncher server address failed to resolve");
        return false;
    }

    if (!resolved) {
        NP_Warn("Couldn't resolve NutPuncher address");
        return false;
    }

    NP_MemzeroRef(NP_PuncherAddr);
    NutPunch_Memcpy(&NP_PuncherAddr, resolved->ai_addr, resolved->ai_addrlen);
    freeaddrinfo(resolved);

    NP_Info("Resolved NutPuncher address");
    return true;
}

static bool NP_MakeNonblocking(NP_Socket sock) {
#ifdef NUTPUNCH_WINDOSE
    u_long argp = 1;
    return !ioctlsocket(sock, FIONBIO, &argp);
#else
    return !fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
#endif
}

static bool NP_MakeReuseAddr(NP_Socket sock) {
#ifdef NUTPUNCH_WINDOSE
    const u_long argp = 1;
#else
    const uint32_t argp = 1;
#endif
    const char* const shit = (const char*)&argp;
    return !setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, shit, sizeof(argp));
}

static bool NP_BindSocket() {
    NP_AddrInfo local = {0};
    NP_LazyInit(), NP_NukeSocket(&NP_Sock);
    NP_Sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (NP_Sock == NUTPUNCH_INVALID_SOCKET) {
        const int err = NP_SockError();
        NP_Warn("Failed to create the underlying UDP socket (%d)", err);
        goto sockfail;
    }

    if (!NP_MakeReuseAddr(NP_Sock)) {
        const int err = NP_SockError();
        NP_Warn("Failed to set socket reuseaddr option (%d)", err);
        goto sockfail;
    }

    if (!NP_MakeNonblocking(NP_Sock)) {
        const int err = NP_SockError();
        NP_Warn("Failed to set socket to non-blocking mode (%d)", err);
        goto sockfail;
    }

    *NP_AddrFamily(&local) = AF_INET;
    *NP_AddrPort(&local) = htons(0);
    *NP_AddrRaw(&local) = htonl(INADDR_ANY);

    if (!bind(NP_Sock, (struct sockaddr*)&local, sizeof(local)))
        return true;

    NP_Warn("Failed to bind a UDP socket (%d)", NP_SockError());
sockfail:
    NP_NukeSocket(&NP_Sock);
    return false;
}

static bool NP_Connect(const char* lobby_id, bool sane, NP_HeartbeatFlagsStorage flags) {
    NP_LazyInit();
    NP_NukeLobbyData();

    if (sane && (!lobby_id || !lobby_id[0])) {
        NP_Warn("Lobby ID cannot be null or empty!");
        NP_LastStatus = NPS_Error;
        return false;
    }

    NP_MemzeroRef(NP_PuncherAddr);
    if (!NP_BindSocket()) {
        NutPunch_Reset(), NP_LastStatus = NPS_Error;
        return false;
    }

    NP_HeartbeatFlags = flags;
    NP_LastBeating = NutPunch_TimeNS();
    NP_ResolveNutpuncher();

    NP_Info("Ready to send heartbeats");
    NP_LastStatus = NPS_Online;
    NP_Memzero(NP_LastError);

    if (lobby_id)
        NutPunch_SNPrintF(NP_LobbyId, sizeof(NP_LobbyId), "%s", lobby_id);
    return true;
}

bool NutPunch_Query() {
    if (!NP_Connect(NULL, false, 0))
        return false;

    NP_Mode = NPNM_Query;
    return true;
}

bool NutPunch_Host(const char* lobby_id) {
    return NP_Connect(lobby_id, true, 0);
}

bool NutPunch_Join(const char* lobby_id) {
    return NP_Connect(lobby_id, true, NP_HB_JoinExisting);
}

bool NutPunch_Grindr(const char* queue_id) {
    NP_LazyInit();

    if (!NP_Connect(NULL, false, 0))
        return false;

    NP_Mode = NPNM_Matchmaking;
    NP_Memzero(NP_QueueId);

    if (queue_id)
        NutPunch_SNPrintF(NP_QueueId, sizeof(NP_QueueId), "%s", queue_id);

    return true;
}

void NutPunch_SetUnlisted(bool unlisted) {
    if (unlisted)
        NP_HeartbeatFlags |= NP_HB_Unlisted;
    else
        NP_HeartbeatFlags &= ~NP_HB_Unlisted;
}

bool NutPunch_IsUnlisted() {
    return NP_Unlisted;
}

void NutPunch_SetMaxPlayers(int players) {
    const int DEFAULT = 4;

    if (players <= 1 || players > NUTPUNCH_MAX_PLAYERS) {
        NP_Warn("Setting %d players max (requested %d)", DEFAULT, players);
        players = DEFAULT;
    }

    NP_HeartbeatFlags &= 0xF;
    NP_HeartbeatFlags |= (players - 1) << 4;
}

int NutPunch_GetMaxPlayers() {
    if (!NutPunch_IsOnline() || NP_MaxPlayers > NUTPUNCH_MAX_PLAYERS)
        return 0;
    return NP_MaxPlayers;
}

void NutPunch_FindLobbies(int filter_count, const NutPunch_Filter* filters) {
    if (filters != NULL && filter_count > NUTPUNCH_MAX_SEARCH_FILTERS) {
        NP_Warn("Filter count exceeded in `NutPunch_FindLobbies`; truncating the input");
        filter_count = NUTPUNCH_MAX_SEARCH_FILTERS;
    }

    static uint8_t query[sizeof(NP_Header) + NUTPUNCH_MAX_SEARCH_FILTERS * sizeof(NutPunch_Filter)]
        = "LIST";
    uint8_t* ptr = query + sizeof(NP_Header);

    if (filter_count > 0 && filters != NULL) {
        NutPunch_Memcpy(ptr, filters, filter_count * sizeof(NutPunch_Filter));
        ptr += filter_count * sizeof(NutPunch_Filter);
    }

    const int len = (int)(ptr - query);
    NP_SendDirectly(NP_PuncherAddr, query, len);
}

void NutPunch_Cleanup() {
    NutPunch_Disconnect();
#ifdef NUTPUNCH_WINDOSE
    WSACleanup();
#endif
}

const char* NutPunch_GetLastError() {
    return NP_LastError;
}

static void NP_HandleEventCb(NutPunch_CallbackEvent event, const void* data) {
    if (NP_Callbacks[event])
        NP_Callbacks[event](data);
}

static const char* NP_FormatSockaddr(NP_AddrInfo addr) {
    static char buf[64] = "";
    NP_Memzero(buf);

    const uint16_t p = ntohs(*NP_AddrPort(&addr));
    const char* const s = inet_ntoa(addr.sin_addr);
    NutPunch_SNPrintF(buf, sizeof(buf), "[%s]:%d", s, p);

    return buf;
}

static void NP_KillPeer(NutPunch_Peer peer) {
    if (NutPunch_PeerAlive(peer))
        NP_HandleEventCb(NPCB_PeerLeft, &peer);
    NP_MemzeroRef(NP_Peers[peer]);
}

static void NP_HandlePing(NP_Message msg) {
    const uint8_t idx = *msg.data++;
    if (idx >= NUTPUNCH_MAX_PLAYERS)
        return;

    NP_PeerInfo* const peer = &NP_Peers[idx];
    peer->last_ping = NutPunch_TimeNS();

    for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
        NutPunch_Field* then = &peer->metadata[i];
        NutPunch_Field* now = (NutPunch_Field*)msg.data;
        msg.data += sizeof(NutPunch_Field);

        if (!NutPunch_Memcmp(then, now, sizeof(NutPunch_Field)))
            continue;

        NutPunch_PeerFieldDiff changed = {0};
        changed.peer = idx, changed.then = *then, changed.now = *now;
        *then = *now;
        NP_HandleEventCb(NPCB_PeerMetadataChanged, &changed);
    }

    if (NP_AddrNull(peer->addr))
        NP_HandleEventCb(NPCB_PeerJoined, &idx);
    peer->addr = msg.addr;

    NP_Trace("PING FROM %d = %s", idx, NP_FormatSockaddr(msg.addr));
}

static void NP_HandleGTFO(NP_Message msg) {
    if (!NP_AddrEq(msg.addr, NP_PuncherAddr))
        return;

    // Have to work around designated array initializers for C++ NutPuncher to compile...
    const char* errors[NPE_Max] = {0};
    errors[NPE_NoSuchLobby] = "Lobby doesn't exist";
    errors[NPE_LobbyExists] = "Lobby already exists";
    errors[NPE_LobbyFull] = "Lobby is full";
    errors[NPE_QueueNoMatch] = "No players to match with";
    errors[NPE_Sybau] = "sybau :wilted_rose:";

    int idx = msg.data[0];
    if (idx <= NPE_Ok || idx >= NPE_Max)
        NP_Warn("Unidentified error");
    else
        NP_Warn("%s", errors[idx]);
    NP_LastStatus = NPS_Error;
}

// this prints out just the public-facing socket address not taking into account the possibility of
// same-NAT peer connections.
static void NP_PrintOurPublicAddress(const uint8_t* data) {
    NP_AddrInfo addr = {0};
    *NP_AddrFamily(&addr) = AF_INET;
    NutPunch_Memcpy(NP_AddrRaw(&addr), data, 4), data += 4;
    *NP_AddrPort(&addr) = *(uint16_t*)data, data += 2;
    NP_Info("Server thinks you are %s", NP_FormatSockaddr(addr));
}

static int NP_SendDirectly(NP_AddrInfo dest, const void* data, int len) {
    struct sockaddr* const shit_dest = (struct sockaddr*)&dest;
    const char* const shit_data = (const char*)data;
    return sendto(NP_Sock, shit_data, len, 0, shit_dest, sizeof(dest));
}

static void NP_SendTimesDirectly(int times, NP_AddrInfo dest, const void* data, int len) {
    while (times-- > 0)
        NP_SendDirectly(dest, data, len);
}

static void NP_PingPeer(int idx, const uint8_t* data) {
    if (NP_Sock == NUTPUNCH_INVALID_SOCKET)
        return;

    if (idx == NP_LocalPeer)
        return;

    NP_AddrInfo pub = {0}, internal = {0};
    *NP_AddrFamily(&pub) = *NP_AddrFamily(&internal) = AF_INET;

    NutPunch_Memcpy(NP_AddrRaw(&pub), data, 4), data += 4;
    *NP_AddrPort(&pub) = *(uint16_t*)data, data += 2;

    NutPunch_Memcpy(NP_AddrRaw(&internal), data, 4), data += 4;
    *NP_AddrPort(&internal) = *(uint16_t*)data, data += 2;

    if (NP_AddrNull(pub) && NP_AddrNull(internal)) { // dead on the NutPuncher's side
        NP_KillPeer(idx);
        return;
    }

    NP_PeerInfo* const peer = &NP_Peers[idx];
    const NutPunch_Clock now = NutPunch_TimeNS(),
                         timeout_period = NUTPUNCH_PEER_TIMEOUT_INTERVAL * NUTPUNCH_MS;

    const bool overtime = now - peer->first_ping >= timeout_period,
               timed_out = peer->first_ping && overtime;

    if (!NutPunch_PeerAlive(idx) && timed_out) {
        NP_Warn("Failed to establish a connection to peer %d", idx + 1);
        peer->first_ping = now;
        return;
    }

    if (!peer->first_ping)
        peer->first_ping = now;

    static uint8_t ping[NP_PING_SIZE] = "PING";
    uint8_t* ptr = &ping[sizeof(NP_Header)];
    *ptr++ = NP_LocalPeer;
    NutPunch_Memcpy(ptr, NP_PeerMetadata, sizeof(NutPunch_Metadata));

    NP_SendDirectly(pub, ping, sizeof(ping));
    NP_Trace("SENT HI %s", NP_FormatSockaddr(pub));

    NP_SendDirectly(internal, ping, sizeof(ping));
    NP_Trace("ALSO TO %s", NP_FormatSockaddr(internal));
}

static void NP_HandleBeating(NP_Message msg) {
    NP_Trace("RECEIVED A BEATING FROM %s", NP_FormatSockaddr(msg.addr));

    if (!NP_AddrEq(msg.addr, NP_PuncherAddr))
        return;
    NP_LastBeating = NutPunch_TimeNS();

    const bool just_joined = NP_LocalPeer == NUTPUNCH_MAX_PLAYERS;
    const NutPunch_Peer old_master = NutPunch_MasterPeer();

    NP_Unlisted = *msg.data++;
    NP_LocalPeer = *msg.data++;
    NP_Master = *msg.data++;
    NP_MaxPlayers = *msg.data++;

    if (NP_LocalPeer >= NUTPUNCH_MAX_PLAYERS) {
        NP_Warn("NutPuncher sent us a junk response?!");
        return;
    }

    // add the join-existing flag after a successful join because otherwise we could get a
    // random-ass disconnection with the "Lobby already exists!" error message.
    NP_HeartbeatFlags |= NP_HB_JoinExisting;

    // sync outgoing max player count with the one we just received.
    NP_HeartbeatFlags &= 0xF;
    NP_HeartbeatFlags |= (NutPunch_GetMaxPlayers() - 1) << 4;

    for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
        NP_PingPeer(i, msg.data);
        if (i == NP_LocalPeer && just_joined)
            NP_PrintOurPublicAddress(msg.data);
        msg.data += 2 * sizeof(NP_PeerAddr);
    }

    for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
        NutPunch_Field* then = &NP_LobbyMetadata[i];
        NutPunch_Field* now = (NutPunch_Field*)msg.data;
        msg.data += sizeof(NutPunch_Field);

        if (!NutPunch_Memcmp(then, now, sizeof(NutPunch_Field)))
            continue;

        NutPunch_FieldDiff diff = {0};
        diff.then = *then, diff.now = *now;
        *then = *now;
        NP_HandleEventCb(NPCB_LobbyMetadataChanged, &diff);
    }

    const NutPunch_Peer new_master = NutPunch_MasterPeer();
    if (old_master != new_master) {
        if (new_master == NutPunch_LocalPeer())
            NP_Info("We're the lobby's master now");
        NP_HandleEventCb(NPCB_NewMaster, &new_master);
    }
}

static void NP_HandleListing(NP_Message msg) {
    if (!NP_AddrEq(msg.addr, NP_PuncherAddr))
        return;
    NP_LastBeating = NutPunch_TimeNS();

    NutPunch_LobbyList list = {0};
    list.count = msg.size / sizeof(NutPunch_LobbyInfo);
    list.lobbies = list.count ? (NutPunch_LobbyInfo*)msg.data : NULL;

    NP_HandleEventCb(NPCB_LobbyList, &list);
}

static void NP_HandleLobbyMetadata(NP_Message msg) {
    if (!NP_AddrEq(msg.addr, NP_PuncherAddr) || msg.size < sizeof(NutPunch_LobbyId))
        return;
    NP_LastBeating = NutPunch_TimeNS();

    NutPunch_LobbyMetadata info = {0};

    NutPunch_Memcpy(info.lobby, msg.data, sizeof(NutPunch_LobbyId));
    msg.data += sizeof(NutPunch_LobbyId);
    info.count = (msg.size - sizeof(NutPunch_LobbyId)) / sizeof(NutPunch_Field);
    info.metadata = info.count ? (NutPunch_Field*)msg.data : NULL;

    NP_HandleEventCb(NPCB_LobbyMetadata, &info);
}

static void NP_HandleData(NP_Message msg) {
    NutPunch_Peer peer_idx = NUTPUNCH_MAX_PLAYERS;

    for (NutPunch_Peer i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
        if (NP_AddrEq(msg.addr, NP_Peers[i].addr)) {
            peer_idx = i;
            break;
        }
    }

    if (peer_idx == NUTPUNCH_MAX_PLAYERS)
        return;

    const NutPunch_Channel channel = *msg.data++;
    msg.size--;

    if (channel > NP_MaxChannel)
        return;

    NP_PeerInfo* const peer = &NP_Peers[peer_idx];
    peer->last_ping = NutPunch_TimeNS();

    const NP_PacketIndex index_as_recv = *(NP_PacketIndex*)msg.data;
    msg.size -= sizeof(index_as_recv), msg.data += sizeof(index_as_recv);

    NP_Data* const packet = (NP_Data*)NutPunch_Malloc(sizeof(*packet));
    packet->data = (uint8_t*)NutPunch_Malloc(msg.size);
    NutPunch_Memcpy(packet->data, msg.data, msg.size);
    packet->destination = peer_idx, packet->size = msg.size;
    packet->index = ntohl(index_as_recv);

    if (!packet->index) {
        packet->next = NP_QueueIn[channel];
        NP_QueueIn[channel] = packet;
        return;
    }

    for (NP_Data* cur = peer->out_of_order[channel]; cur; cur = cur->next)
        if (cur->index == packet->index) // check for duplicates
            return;

    packet->next = peer->out_of_order[channel];
    peer->out_of_order[channel] = packet;

    for (;;) {
        NP_Data *cur = peer->out_of_order[channel], *prev = NULL;
        for (; cur; prev = cur, cur = cur->next)
            if (cur->index == peer->recv_counter + 1)
                break;
        if (!cur)
            break;

        peer->recv_counter++;
        if (prev)
            prev->next = cur->next;
        else
            peer->out_of_order[channel] = cur->next;

        cur->next = NP_QueueIn[channel];
        NP_QueueIn[channel] = cur;
    }

    static uint8_t acky[sizeof(NP_Header) + sizeof(NP_Acky)] = "ACKY";
    uint8_t* ptr = acky + sizeof(NP_Header);
    *ptr++ = channel;
    NutPunch_Memcpy(ptr, &index_as_recv, sizeof(index_as_recv));
    NP_SendDirectly(peer->addr, acky, sizeof(acky));
}

static void NP_HandleAcky(NP_Message msg) {
    const NutPunch_Channel channel = *(NutPunch_Channel*)msg.data++;
    const NP_PacketIndex index = ntohl(*(NP_PacketIndex*)msg.data++);
    for (NP_Data* ptr = NP_QueueOut[channel]; ptr; ptr = ptr->next) {
        if (ptr->index == index) {
            ptr->f_delete = true;
            return;
        }
    }
}

static void NP_HandlePong(NP_Message msg) {
    (void)msg;
    if (NP_AddrEq(msg.addr, NP_PuncherAddr))
        NP_LastBeating = NutPunch_TimeNS();
}

static void NP_HandleDate(NP_Message msg) {
    if (NP_Mode != NPNM_Matchmaking)
        return;

    if (!NP_AddrEq(msg.addr, NP_PuncherAddr))
        return;

    NP_Connect((char*)msg.data, true, NP_HB_Grindr);
    NP_HandleEventCb(NPCB_QueueCompleted, msg.data);
}

static bool NP_SendHeartbeat() {
    if (NP_Sock == NUTPUNCH_INVALID_SOCKET)
        return true;

    static uint8_t heartbeat[sizeof(NP_Header) + sizeof(NP_Heartbeat)] = {0};

    switch (NP_Mode) {
    default:
        break;

    case NPNM_Normal: {
        NP_Memzero(heartbeat);
        uint8_t* ptr = heartbeat;

        NutPunch_Memcpy(ptr, "JOIN", sizeof(NP_Header));
        ptr += sizeof(NP_Header);

        NutPunch_Memcpy(ptr, NP_PeerId, sizeof(NutPunch_PeerId));
        ptr += sizeof(NutPunch_PeerId);

        NutPunch_Memcpy(ptr, NP_LobbyId, sizeof(NutPunch_LobbyId));
        ptr += sizeof(NutPunch_LobbyId);

        NP_AddrInfo addr = {0};
        socklen_t addr_size = sizeof(addr);
        getsockname(NP_Sock, (struct sockaddr*)&addr, &addr_size);

        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        NutPunch_Memcpy(ptr, NP_AddrRaw(&addr), 4), ptr += 4;
        NutPunch_Memcpy(ptr, NP_AddrPort(&addr), 2), ptr += 2;

        *(NP_HeartbeatFlagsStorage*)ptr = NP_HeartbeatFlags,
        ptr += sizeof(NP_HeartbeatFlagsStorage);

        NutPunch_Memcpy(ptr, NP_LobbyMetadata, sizeof(NutPunch_Metadata));
        ptr += sizeof(NutPunch_Metadata);

        const int len = (int)(ptr - heartbeat);
        if (0 <= NP_SendDirectly(NP_PuncherAddr, heartbeat, len))
            return true;
        break;
    }

    case NPNM_Matchmaking: {
        NP_Memzero(heartbeat);
        uint8_t* ptr = heartbeat;

        NutPunch_Memcpy(ptr, "FIND", sizeof(NP_Header));
        ptr += sizeof(NP_Header);

        NutPunch_Memcpy(ptr, NP_PeerId, sizeof(NutPunch_PeerId));
        ptr += sizeof(NutPunch_PeerId);

        NutPunch_Memcpy(ptr, NP_QueueId, sizeof(NutPunch_QueueId));
        ptr += sizeof(NutPunch_QueueId);

        const int len = (int)(ptr - heartbeat);
        if (0 <= NP_SendDirectly(NP_PuncherAddr, heartbeat, len))
            return true;
        break;
    }
    }

    const int err = NP_SockError();
    switch (err) {
    case NP_WouldBlock:
    case NP_ConnReset:
        break;

    default:
        if (NP_Mode == NPNM_Query)
            break;
        NP_Warn("Failed to send heartbeat to NutPuncher (%d)", err);
        return false;
    }

    return true;
}

typedef enum {
    NP_RS_SockFail,
    NP_RS_Again,
    NP_RS_Done,
} NP_ReceiveStatus;

static int NP_UglyRecvfrom(NP_AddrInfo* addr, uint8_t* buf, int size) {
    socklen_t addr_size = sizeof(*addr);
    struct sockaddr* const shit_addr = (struct sockaddr*)addr;
    return recvfrom(NP_Sock, (char*)buf, size, 0, shit_addr, &addr_size);
}

static NP_ReceiveStatus NP_ReceiveShit() {
    // happens after handling a GTFO:
    if (NP_LastStatus == NPS_Error || NP_Sock == NUTPUNCH_INVALID_SOCKET)
        return NP_RS_Done;

    static uint8_t buf[NUTPUNCH_BUFFER_SIZE] = {0};
    NP_AddrInfo addr = {0};

    int size = NP_UglyRecvfrom(&addr, buf, sizeof(buf));
    if (size < 0) {
        const int err = NP_SockError();
        if (err == NP_WouldBlock || err == NP_ConnReset)
            return NP_RS_Done;
        else if (err == NP_TooFat)
            return NP_RS_Again;
        NP_Warn("Failed to receive data (%d)", err);
        return NP_RS_SockFail;
    }

    size -= sizeof(NP_Header);
    if (size < 0)
        return NP_RS_Again; // junk
    NP_Trace("RECEIVED %d BYTES OF SHIT", size);

    const size_t len = sizeof(NP_MessageTypes) / sizeof(*NP_MessageTypes);
    for (int i = 0; i < len; i++) {
        const NP_MessageType type = NP_MessageTypes[i];

        if (type.packet_size != NP_ANY_LEN && size != type.packet_size)
            continue;
        if (NutPunch_Memcmp(buf, type.identifier, sizeof(NP_Header)))
            continue;

        NP_Message msg = {0};
        msg.addr = addr, msg.size = size;
        msg.data = (uint8_t*)(buf + sizeof(NP_Header));
        type.handler(msg);
        break;
    }

    return NP_RS_Again;
}

static void NP_PruneChannelOutQueue(const NutPunch_Channel channel) {
    for (NP_Data *ptr = NP_QueueOut[channel], *prev = NULL; ptr;) {
        if (!ptr->f_delete) {
            prev = ptr, ptr = ptr->next;
            continue;
        }

        if (prev)
            prev->next = ptr->next;
        else
            NP_QueueOut[channel] = ptr->next;

        NP_Data* const tmp = ptr;
        ptr = ptr->next;
        NutPunch_Free(tmp->data), NutPunch_Free(tmp);
    }
}

static void NP_PruneOutQueue() {
    for (int i = 0; i < NUTPUNCH_CHANNEL_COUNT; i++)
        NP_PruneChannelOutQueue(i);
}

static void NP_FlushChannelOutQueue(const NutPunch_Channel channel) {
    const NutPunch_Clock now = NutPunch_TimeNS(),
                         bounce_interval = NUTPUNCH_BOUNCE_INTERVAL * NUTPUNCH_MS;

    for (NP_Data* cur = NP_QueueOut[channel]; cur; cur = cur->next) {
        if (!NutPunch_PeerAlive(cur->destination)) {
            cur->f_delete = true;
            continue;
        }

        if (cur->index && cur->sent_at && now - cur->sent_at < bounce_interval)
            continue;

        cur->sent_at = now;
        cur->f_delete = !cur->index;

        if (NP_Sock == NUTPUNCH_INVALID_SOCKET) {
            cur->f_delete = true;
            continue;
        }

        const int result
            = NP_SendDirectly(NP_Peers[cur->destination].addr, cur->data, (int)cur->size);
        if (!result)
            NP_KillPeer(cur->destination);

        if (result >= 0 || NP_SockError() == NP_WouldBlock || NP_SockError() == NP_ConnReset)
            continue;

        NP_Warn("Failed to send data to peer #%d (%d)", cur->destination + 1, NP_SockError());
        NP_NukeLobbyData();
        return;
    }
}

static void NP_FlushOutQueue() {
    for (int i = 0; i < NUTPUNCH_CHANNEL_COUNT; i++)
        NP_FlushChannelOutQueue(i);
}

static void NP_SendGoodbyes() {
    if (NP_Mode == NPNM_Query)
        return;

    static uint8_t bye[sizeof(NP_Header) + sizeof(NutPunch_PeerId)] = "DISC";
    NutPunch_Memcpy(bye + sizeof(NP_Header), NP_PeerId, sizeof(NutPunch_PeerId));
    NP_SendTimesDirectly(10, NP_PuncherAddr, bye, sizeof(bye));
}

void NutPunch_Flush() {
    if (NP_Closing)
        NP_SendGoodbyes();
    NP_PruneOutQueue(), NP_FlushOutQueue();
}

void NutPunch_Register(NutPunch_CallbackEvent event, NutPunch_Callback cb) {
    if (event < NPCB_Count)
        NP_Callbacks[event] = cb;
}

static void NP_NetworkUpdate() {
    NutPunch_Clock now = NutPunch_TimeNS(),
                   server_timeout = NUTPUNCH_SERVER_TIMEOUT_INTERVAL * NUTPUNCH_MS,
                   peer_timeout = NUTPUNCH_PEER_TIMEOUT_INTERVAL * NUTPUNCH_MS;

    if (NP_Mode != NPNM_Query && (now - NP_LastBeating) >= server_timeout) {
        NP_Warn("NutPuncher connection timed out!");
        goto error;
    }

    for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
        const NutPunch_Clock diff = now - NP_Peers[i].last_ping;
        const bool timed_out = diff >= peer_timeout;

        if (i == NutPunch_LocalPeer())
            continue;

        if (!timed_out || !NutPunch_PeerAlive(i))
            continue;

        NP_Info("Peer %d timed out", i + 1);
        NP_KillPeer(i);
    }

    if (!NP_SendHeartbeat())
        goto error;

    for (;;) {
        switch (NP_ReceiveShit()) {
        case NP_RS_SockFail:
            goto error;
        case NP_RS_Done:
            NutPunch_Flush();
            return;
        case NP_RS_Again:
            break;
        }
    }

error:
    NP_LastStatus = NPS_Error;
}

NutPunch_UpdateStatus NutPunch_Update() {
    NP_LazyInit();

    if (NP_LastStatus == NPS_Idle || NP_Sock == NUTPUNCH_INVALID_SOCKET)
        return NPS_Idle;

    NP_LastStatus = NPS_Online;
    NP_NetworkUpdate();

    if (NP_LastStatus == NPS_Error) {
        NutPunch_Disconnect();
        return NPS_Error;
    }

    return NP_LastStatus;
}

void NutPunch_Disconnect() {
    NP_Info("Disconnecting from lobby (if any)");
    if (NutPunch_IsOnline()) // send a disconnection packet too
        NP_Closing = true, NP_NetworkUpdate();
    NutPunch_Reset();
}

void NutPunch_SetChannelCount(int max) {
    if (max < 1 || max > NUTPUNCH_CHANNEL_COUNT)
        return;

    NP_MaxChannel = max - 1;
    for (int i = max; i < NUTPUNCH_CHANNEL_COUNT; i++)
        NP_CleanupPackets(&NP_QueueIn[i]);
}

bool NutPunch_HasMessage(NutPunch_Channel channel) {
    return NP_QueueIn[channel] != NULL;
}

int NutPunch_NextMessage(NutPunch_Channel channel, void* out, int* size) {
    NP_Data* const packet = NP_QueueIn[channel];
    if (!packet) {
        NP_Warn("You forgot to check `NutPunch_HasMessage(%d)`", channel);
        return NUTPUNCH_MAX_PLAYERS;
    }

    if (*size < packet->size) {
        NP_Warn("Not enough memory allocated to copy the next packet");
        return NUTPUNCH_MAX_PLAYERS;
    }

    if (size)
        *size = (int)packet->size;
    if (out)
        NutPunch_Memcpy(out, packet->data, packet->size);
    NutPunch_Free(packet->data);

    int source_peer = packet->destination;
    if (source_peer > NUTPUNCH_MAX_PLAYERS)
        source_peer = NUTPUNCH_MAX_PLAYERS;

    NP_Data* const next = packet->next;
    NutPunch_Free(packet);
    NP_QueueIn[channel] = next;

    return source_peer;
}

static void NP_SendPro(const NutPunch_Peer destination, const NutPunch_Channel channel,
    const void* data, const int size, const bool reliable) {
    NP_LazyInit();

    if (!data) {
        NP_Warn("No data?");
        return;
    }

    if (!NutPunch_PeerAlive(destination) || destination == NutPunch_LocalPeer())
        return;

    if (size > NUTPUNCH_BUFFER_SIZE - 32) {
        NP_Warn("Ignoring a huge packet");
        return;
    }

    NP_PacketIndex index = 0;
    if (reliable)
        index = ++NP_Peers[destination].send_counter;
    const NP_PacketIndex net_index = htonl(index);

    static uint8_t buf[NUTPUNCH_BUFFER_SIZE] = "DATA";
    uint8_t* ptr = buf + sizeof(NP_Header);

    *ptr++ = channel;
    NutPunch_Memcpy(ptr, &net_index, sizeof(net_index)), ptr += sizeof(net_index);
    NutPunch_Memcpy(ptr, data, size), ptr += size;

    NP_Data* const next = NP_QueueOut[channel];
    NP_QueueOut[channel] = (NP_Data*)NutPunch_Malloc(sizeof(*next));

    NP_Data* const cur = NP_QueueOut[channel];
    cur->next = next, cur->destination = destination;
    cur->size = ptr - buf, cur->index = index, cur->f_delete = false;
    cur->data = (uint8_t*)NutPunch_Malloc(cur->size);
    NutPunch_Memcpy(cur->data, buf, cur->size);
}

void NutPunch_Send(NutPunch_Channel channel, NutPunch_Peer peer, const void* data, int size) {
    NP_SendPro(peer, channel, data, size, false);
}

void
NutPunch_SendReliably(NutPunch_Channel channel, NutPunch_Peer peer, const void* data, int size) {
    NP_SendPro(peer, channel, data, size, true);
}

int NutPunch_PeerCount() {
    int count = 0;
    for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
        count += NutPunch_PeerAlive(i);
    return count;
}

bool NutPunch_PeerAlive(NutPunch_Peer peer) {
    if (!NutPunch_IsReady() || peer >= NUTPUNCH_MAX_PLAYERS)
        return false;
    if (NutPunch_LocalPeer() == peer)
        return true;
    return !NP_AddrNull(NP_Peers[peer].addr);
}

int NutPunch_LocalPeer() {
    if (!NutPunch_IsOnline())
        return NUTPUNCH_MAX_PLAYERS;
    return NP_LocalPeer;
}

int NutPunch_MasterPeer() {
    if (!NutPunch_IsOnline())
        return NUTPUNCH_MAX_PLAYERS;
    return NP_Master;
}

bool NutPunch_IsOnline() {
    return NP_LastStatus == NPS_Online;
}

bool NutPunch_IsReady() {
    return NutPunch_LocalPeer() != NUTPUNCH_MAX_PLAYERS;
}

const char* NutPunch_Basename(const char* path) {
    size_t len = 0; // clang-format off
	for (len = 0; path[len]; len++) {} // clang-format on
    for (size_t i = len - 2; i >= 0; i--)
        if (path[i] == '/' || path[i] == '\\')
            return &path[i + 1];
    return path;
}

// `TIME_UTC` & `timespec_get` polyfill for picky compilers.
#ifndef TIME_UTC

#define TIME_UTC 1

static int timespec_get(struct timespec* ts, int base) {
    if (base == TIME_UTC)
        return clock_gettime(CLOCK_REALTIME, ts) ? 0 : base;
    return 0;
}

#endif // TIME_UTC

NutPunch_Clock NutPunch_TimeNS() {
    struct timespec ts = {0};
    timespec_get(&ts, TIME_UTC);
    return (NutPunch_Clock)ts.tv_sec * NUTPUNCH_SEC + (NutPunch_Clock)ts.tv_nsec;
}

#ifdef NUTPUNCH_WINDOSE

#define NP_SleepMs(ms) Sleep(ms)

#else

static void NP_SleepMs(int ms) {
    // Stolen from: <https://stackoverflow.com/a/1157217>
    struct timespec ts = {0};
    ts.tv_sec = ms / 1000, ts.tv_nsec = (ms % 1000) * (NUTPUNCH_SEC / 1000);
    int res = 0;
    do { res = nanosleep(&ts, &ts); } while (res && errno == EINTR);
}

#endif // NUTPUNCH_WINDOSE

#endif // NUTPUNCH_IMPLEMENTATION

#ifdef __cplusplus
}
#endif

#endif // NUTPUNCH_H
