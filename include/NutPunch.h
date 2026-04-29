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

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

typedef SOCKET NP_Sock;
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

typedef int64_t NP_Sock;
#define NUTPUNCH_INVALID_SOCKET (-1)

#define NP_SockError() errno
#define NP_WouldBlock EWOULDBLOCK
#define NP_ConnReset ECONNRESET
#define NP_TooFat EMSGSIZE

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

/// The maximum amount of results `NutPunch_LobbyList` can provide.
#define NUTPUNCH_MAX_SEARCH_RESULTS (16)

/// The maximum amount of filters you can pass to `NutPunch_FindLobbies`.
#define NUTPUNCH_MAX_SEARCH_FILTERS (8)

/// Maximum length of a metadata field name.
#define NUTPUNCH_FIELD_NAME_MAX (16)

/// Maximum volume of data you can store in a metadata field.
#define NUTPUNCH_FIELD_DATA_MAX (32)

/// Maximum amount of metadata fields per lobby/player.
#define NUTPUNCH_MAX_FIELDS (8)

/// The maximum amount of channels a NutPunch host can send to/receive on.
#define NUTPUNCH_MAX_CHANNELS (30)

/// Maximum amount of bytes a packet fragment can hold.
#define NUTPUNCH_FRAGMENT_SIZE (1024) // TODO: actually implement fragmenting

/// How many times to attempt resending a reliable packet.
#define NUTPUNCH_MAX_RETRIES (16)

/// Initial amount of milliseconds to wait before resending a reliable packet.
/// The retransmission time will also increase by this much after resending.
#define NUTPUNCH_RETRY_INTERVAL ((NutPunch_Clock)200)

/// How many milliseconds to wait for a peer or the NutPuncher to respond before timing out.
#define NUTPUNCH_TIMEOUT_INTERVAL ((NutPunch_Clock)5000)

#ifndef NUTPUNCH_NOSTD
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h> // IWYU pragma: export
#endif

extern char NP_LastError[512];

#ifndef NutPunch_Log
#include <stdio.h>
#define NutPunch_Log(msg, ...)                                                                     \
    do {                                                                                           \
        fprintf(                                                                                   \
            stdout, "(%s:%d) " msg "\n", NutPunch_Basename(__FILE__), __LINE__, ##__VA_ARGS__);    \
        fflush(stdout);                                                                            \
    } while (0)
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

#if !defined(NutPunch_Memcpy) && !defined(NutPunch_Memset) && !defined(NutPunch_Memcmp)            \
    && !defined(NutPunch_StrNCmp)

#include <string.h>
#define NutPunch_Memcpy memcpy
#define NutPunch_Memset memset
#define NutPunch_Memcmp memcmp
#define NutPunch_StrNCmp strncmp

#elif !defined(NutPunch_Memcpy) || !defined(NutPunch_Memset) || !defined(NutPunch_Memcmp)          \
    || !defined(NutPunch_StrNCmp)

#error Define NutPunch_Mem{cpy,set,cmp} & NutPunch_StrNCmp together!

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
#else
#define NP_Trace(...)
#endif

#ifdef NUTPUNCH_WINDOSE
#define NP_SleepMs Sleep
#else
void NP_SleepMs(int ms);
#endif

/// The internal unique identifier for your peer. You don't actually interact with it in your code.
typedef char NutPunch_PeerId[8];

/// A string uniquely identifying a NutPunch lobby.
typedef char NutPunch_LobbyId[16];

/// An identifier used in matchmaking for matching you with other peers on the same queue.
typedef char NutPunch_QueueId[16];

typedef uint8_t NutPunch_Channel, NutPunch_Peer;

/// A linked-list of of lobby/peer metadata.
typedef struct NutPunch_Field {
    char name[NUTPUNCH_FIELD_NAME_MAX]; // TODO: typedef these bad boys...
    char data[NUTPUNCH_FIELD_DATA_MAX];
    struct NutPunch_Field* next;
} NutPunch_Field;

/// A struct containing a comparison between previous and updated metadata.
typedef struct {
    char name[NUTPUNCH_FIELD_NAME_MAX];
    char then[NUTPUNCH_FIELD_DATA_MAX], now[NUTPUNCH_FIELD_DATA_MAX];
} NutPunch_FieldDiff;

/// Same as `NutPunch_FieldDiff`, but with a peer identifier.
typedef struct {
    char name[NUTPUNCH_FIELD_NAME_MAX];
    char then[NUTPUNCH_FIELD_DATA_MAX], now[NUTPUNCH_FIELD_DATA_MAX];
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

/// A snapshot of a lobby's metadata returned by the NutPuncher.
typedef struct {
    NutPunch_LobbyId lobby;
    NutPunch_Field* metadata;
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
    /// Callback data: see `NutPunch_FieldDiff`.
    NPCB_LobbyMetadataChanged,
    /// Callback data: see `NutPunch_PeerFieldDiff`.
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

/// Sets a custom NutPuncher server address.
void NutPunch_SetServerAddr(const char* hostname);

/// Connects to NutPuncher without joining a lobby, mainly for lobby queries. Return `false` if a
/// network error occurs and `true` otherwise.
bool NutPunch_QueryMode();

/// Joins a lobby by its ID. Return `false` if a network error occurs and `true` otherwise.
///
/// If no lobby exists with this ID, an error status spits out of `NutPunch_Update()` rather than
/// immediately here.
bool NutPunch_Join(const char* lobby_id);

/// Hosts a lobby with the specified ID. Return `false` if a network error occurs and `true`
/// otherwise.
///
/// If a lobby with the same ID exists, an error status spits out of `NutPunch_Update()` rather
/// than immediately here.
bool NutPunch_Host(const char* lobby_id);

/// Joins a matchmaking queue. The queue ID describes your game to find similar peers. Returns
/// `false` if a network error occurs and `true` otherwise.
bool NutPunch_EnterQueue(const char* queue_id);

/// Returns the remaining time before getting kicked out of a queue in seconds.
int NutPunch_QueueTime();

/// Returns the amount of peers waiting in the same queue.
int NutPunch_QueueCount();

/// Unlists the lobby you're the master of. Do this immediately after calling `NutPunch_Host`.
void NutPunch_SetUnlisted(bool);

/// Returns `true` if the current lobby is unlisted.
bool NutPunch_IsUnlisted();

/// Changes the maximum player count. Do this immediately after calling `NutPunch_Host`.
void NutPunch_SetMaxPlayers(int players);

/// Returns the maximum player count of the lobby you are in. Returns 0 if you aren't in a lobby.
int NutPunch_GetMaxPlayers();

/// Registers an event handler. The callback functions are called during `NutPunch_Update()`.
void NutPunch_Register(NutPunch_CallbackEvent event, NutPunch_Callback cb);

/// Call this at the end of your program to disconnect gracefully and run other semi-important
/// cleanup routines.
void NutPunch_Shutdown();

/// Call this every frame to update NutPunch. Returns one of the `NPS_*` constants you need to match
/// against to see if something goes wrong.
NutPunch_UpdateStatus NutPunch_Update();

/// Sends all queued outgoing packets early (before a `NutPunch_Update()`). Useful in niche cases.
void NutPunch_Flush();

/// Requests metadata from a lobby. If you aren't connected to the NutPuncher, call
/// `NutPunch_Query()` first.
/// Triggers a `NPCB_LobbyMetadata` callback if successful.
void NutPunch_RequestLobbyData(const NutPunch_LobbyId lobby);

/// Returns metadata from the lobby we're in.
const char* NutPunch_GetLobbyData(const char* name);

/// Returns metadata a peer reported to us.
const char* NutPunch_GetPeerData(NutPunch_Peer peer, const char* name);

/// Sets metadata for the lobby. Doesn't do anything if you aren't the master.
///
/// See `NUTPUNCH_FIELD_NAME_MAX` and `NUTPUNCH_FIELD_DATA_MAX` for the maximum amounts of data you
/// can squeeze into a field.
void NutPunch_SetLobbyData(const char* name, const char* data);

/// Sets metadata for this peer.
///
/// See `NUTPUNCH_FIELD_NAME_MAX` and `NUTPUNCH_FIELD_DATA_MAX` for the maximum amounts of data you
/// can squeeze into a field.
void NutPunch_SetPeerData(const char* name, const char* data);

/// Sets the maximum amount of channels this peer can receive from. Packets with an index higher
/// than or equal to maximum channel count are discarded silently.
///
/// WARNING: make sure to set the channel count BEFORE you try `NutPunch_Host` or `NutPunch_Join`.
/// Otherwise, the default channel count will be used once you connect.
///
/// The default behavior is receiving only on channel 0.
///
/// Setting 0 channels or more than `NUTPUNCH_MAX_CHANNELS` fails silently.
void NutPunch_SetChannelCount(int);

/// Checks if there is a packet waiting in the receiving queue for the specified channel index.
///
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

/// Sends data on the specified channel, to the specified peer. See `NutPunch_SendReliably` for
/// reliable packet delivery.
void NutPunch_Send(NutPunch_Channel, NutPunch_Peer, const void*, int);

/// Sends data on the specified channel, to the specified peer, expecting the remote side to
/// acknowledge the fact of reception. Resends the packet up to `NUTPUNCH_MAX_RETRIES` times.
void NutPunch_SendReliably(NutPunch_Channel, NutPunch_Peer, const void*, int);

/// Counts how many "live" peers we have a route to, including our local peer.
///
/// Do not use this as an upper bound for iterating over peers. Iterate from 0 to
/// `NUTPUNCH_MAX_PLAYERS` and check each peer individually with `NutPunch_PeerAlive`.
int NutPunch_PeerCount();

/// Returns `true` if you are connected to the peer with the specified index.
///
/// Use `NUTPUNCH_MAX_PLAYERS` as the upper bound for iterating, and check each peer's status
/// individually using this function.
bool NutPunch_PeerAlive(NutPunch_Peer);

/// Returns the local peer's index. Available only after successfully joining a lobby. Returns
/// `NUTPUNCH_MAX_PLAYERS` if this fails for any reason.
int NutPunch_LocalPeer();

/// Returns the master peer's index. Available only after successfully joining a lobby. Returns
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

/// Searches for lobbies. If you aren't connected to the NutPuncher, call `NutPunch_Query()` first.
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

/// Returns the human-readable description of the latest error that occurred in `NutPunch_Update()`
/// or several other internal functions.
const char* NutPunch_GetLastError();

/// Returns a substring of `path` without its directory name. A utility function used internally in
/// the default implementation of `NutPunch_Log`.
const char* NutPunch_Basename(const char* path);

// gross implementation details follow.....

#define NUTPUNCH_SEC ((NutPunch_Clock)1000000000)
#define NUTPUNCH_MS (NUTPUNCH_SEC / ((NutPunch_Clock)1000))

typedef uint64_t NutPunch_Clock;

#ifndef NutPunch_TimeNS
NutPunch_Clock NutPunch_TimeNS();
#endif

typedef struct sockaddr_in NP_SockAddr;

typedef uint8_t NP_NetMode;
enum {
    NPNM_Normal,
    NPNM_Query,
    NPNM_Matchmaking,
};

typedef uint8_t NP_HeartbeatFlagsStorage;
typedef uint8_t NP_Header[4];

// tightly packed structs matching packet layouts.
//
// FOR THE FUTURE GENERATIONS: PACKET STRUCTS GO HERE!!!!!!

#pragma pack(push, 1)

/// The metadata field payload. Don't confuse with `NutPunch_Field`.
typedef struct {
    char name[NUTPUNCH_FIELD_NAME_MAX];
    char data[NUTPUNCH_FIELD_DATA_MAX];
} NP_Field;

typedef NP_Field NP_Metadata[NUTPUNCH_MAX_FIELDS];

typedef struct {
    uint32_t ip;
    uint16_t port;
} NP_PeerAddr;

typedef struct {
    uint8_t unlisted;
    NutPunch_Peer local, master, count, capacity;
} NP_Beating;

typedef struct {
    NP_Beating base;
    struct {
        NutPunch_Peer index;
        NP_PeerAddr pub, same_nat;
    } peers[NUTPUNCH_MAX_PLAYERS];
    NP_Metadata lobby_metadata;
} NP_BeatingAppend;

typedef struct {
    NutPunch_PeerId peer;
    NutPunch_LobbyId lobby;
    NP_HeartbeatFlagsStorage flags;
    NP_PeerAddr same_nat;
} NP_Heartbeat;

typedef struct {
    NutPunch_PeerId peer;
    NutPunch_QueueId queue;
} NP_Find;

#pragma pack(pop)

enum {
    NP_HB_JoinExisting = 1 << 0,
    NP_HB_Unlisted = 1 << 1,
    NP_HB_Queue = 1 << 2,
};

// BATSHIT CRAZY BATSHIT!!!
#ifdef NUTPUNCH_IMPLEMENTATION

typedef struct {
    NutPunch_Field* metadata;
    NP_SockAddr address;
    NutPunch_Clock last_ping;
} NP_PeerInfo;

typedef struct {
    NP_SockAddr from;
    const uint8_t* data;
    size_t len;
} NP_Message;

typedef struct NP_IncomingData {
    NutPunch_Peer peer;
    void* data;
    size_t len;
    struct NP_IncomingData* next;
} NP_IncomingData;

typedef struct NP_OutgoingPacket {
    NP_SockAddr destination;
    uint8_t* data;
    struct NP_OutgoingPacket* next;
    int len, retries;
    NutPunch_Clock last_retry;
    bool acked;
    uint32_t id;
} NP_OutgoingPacket;

typedef struct {
    const char identifier[sizeof(NP_Header) + 1];
    void (*const handle)(NP_Message);
    const int64_t min_packet_size;
} NP_MessageType;

static void NP_HandlePing(NP_Message), NP_HandleGTFO(NP_Message), NP_HandleBeating(NP_Message),
    NP_HandleListing(NP_Message), NP_HandleLobbyData(NP_Message), NP_HandleData(NP_Message),
    NP_HandleQueue(NP_Message), NP_HandleDate(NP_Message), NP_HandleAcky(NP_Message);

static const NP_MessageType NP_MessageTypes[] = {
    {"ACKY", NP_HandleAcky,      4                       },
    {"PING", NP_HandlePing,      1                       },
    {"LIST", NP_HandleListing,   0                       },
    {"LGMA", NP_HandleLobbyData, sizeof(NutPunch_LobbyId)},
    {"DATA", NP_HandleData,      1                       },
    {"GTFO", NP_HandleGTFO,      1                       },
    {"BEAT", NP_HandleBeating,   sizeof(NP_Beating)      },
    {"QUEU", NP_HandleQueue,     1 + 1                   },
    {"DATE", NP_HandleDate,      sizeof(NutPunch_LobbyId)},
};

char NP_LastError[512] = "";

static bool NP_InitDone = false, NP_Closing = false;
static NutPunch_UpdateStatus NP_LastStatus = NPS_Idle;

static NP_Sock NP_Socket = NUTPUNCH_INVALID_SOCKET;
static NutPunch_Clock NP_LastBeating = 0;

static char NP_LobbyId[sizeof(NutPunch_LobbyId) + 1] = {0};
static NutPunch_PeerId NP_PeerId = {0};
static NutPunch_QueueId NP_QueueId = {0};

static NP_PeerInfo NP_Peers[NUTPUNCH_MAX_PLAYERS] = {0};
static NutPunch_Peer NP_LocalPeer = NUTPUNCH_MAX_PLAYERS, NP_Master = NUTPUNCH_MAX_PLAYERS,
                     NP_MaxPlayers = 0;

static NutPunch_Callback NP_Callbacks[NPCB_Count] = {0};

static NP_SockAddr NP_ServerAddr = {0};
static char NP_ServerHost[128] = {0};

static NutPunch_Channel NP_ChannelCount = 1;
static NP_IncomingData* NP_Unread[NUTPUNCH_MAX_CHANNELS] = {0};
static NP_OutgoingPacket* NP_Pending = NULL;

static bool NP_Unlisted = false;

static NP_NetMode NP_Mode = NPNM_Normal;
static NP_HeartbeatFlagsStorage NP_HeartbeatFlags = 0;
static int NP_QueueCount = 0, NP_QueueTime = 0;

static NutPunch_Field *NP_LobbyMetadata = NULL, *NP_PeerMetadata = NULL;

static const char* NP_FormatSockaddr(NP_SockAddr addr) {
    static char buf[64] = "";

    const char* s = inet_ntoa(addr.sin_addr);
    NutPunch_SNPrintF(buf, sizeof(buf), "[%s]:%d", s, ntohs(addr.sin_port));

    return buf;
}

static void NP_JustSend(NP_SockAddr destination, const void* data, size_t len, bool reliable) {
    const int prefix = 4;

    if (prefix + len > NUTPUNCH_FRAGMENT_SIZE) {
        NP_Warn("Ignoring a huge packet");
        return;
    }

    static uint32_t counter = 0;

    NP_OutgoingPacket* last = NP_Pending;
    for (; last && last->next; last = last->next) {}

    last = *(last ? &last->next : &NP_Pending) = (NP_OutgoingPacket*)NutPunch_Malloc(sizeof(*last));
    last->destination = destination, last->next = NULL;
    last->retries = reliable ? 0 : -1, last->last_retry = 0, last->acked = false;

    last->id = reliable ? ++counter : 0;
    last->len = prefix + (int)len;

    // prefixing the entire packet with an id...
    last->data = (uint8_t*)NutPunch_Malloc(last->len);
    *(uint32_t*)last->data = htonl(last->id);
    NutPunch_Memcpy(last->data + prefix, data, len);

    NP_Trace("GONNA SEND %i BYTES TO %s", last->len, NP_FormatSockaddr(destination));
}

static void NP_JustSpam(NP_SockAddr destination, const void* data, size_t len) {
    for (int times = 5; times > 0; times--)
        NP_JustSend(destination, data, len, false);
}

void NP_NukeSocket(NP_Sock* sock) {
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

static void NP_NukeMetadata(NutPunch_Field** metadata) {
    while (metadata && *metadata) {
        NutPunch_Field* ptr = *metadata;
        *metadata = ptr->next;
        NutPunch_Free(ptr);
    }
}

static void NP_NukePeer(NutPunch_Peer peer) {
    NP_PeerInfo* ptr = &NP_Peers[peer];
    NP_NukeMetadata(&ptr->metadata);
    NP_MemzeroRef(*ptr);
}

static void NP_NukeLobbyDataLite() {
    NP_Closing = NP_Unlisted = false;
    NP_LocalPeer = NP_Master = NUTPUNCH_MAX_PLAYERS;

    NP_Mode = NPNM_Normal;
    NP_HeartbeatFlags = NP_QueueCount = NP_QueueTime = 0;
    NP_LobbyId[0] = 0;

    for (NutPunch_Peer i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
        NP_NukePeer(i);

    NutPunch_SetMaxPlayers(NUTPUNCH_MAX_PLAYERS);

    for (size_t i = 0; i < NUTPUNCH_MAX_CHANNELS; i++) {
        while (NP_Unread[i]) {
            NP_IncomingData* ptr = NP_Unread[i];
            NP_Unread[i] = ptr->next;
            NutPunch_Free(ptr->data), NutPunch_Free(ptr);
        }
    }

    while (NP_Pending) {
        NP_OutgoingPacket* ptr = NP_Pending;
        NP_Pending = ptr->next;
        NutPunch_Free(ptr->data), NutPunch_Free(ptr);
    }

    NP_NukeSocket(&NP_Socket);
}

static void NP_NukeLobbyData() {
    NP_NukeLobbyDataLite();
    NP_NukeMetadata(&NP_LobbyMetadata);
    NP_NukeMetadata(&NP_PeerMetadata);
}

static void NP_ResetImpl() {
    NP_NukeLobbyData();
    NP_MemzeroRef(NP_ServerAddr);
    NP_LastStatus = NPS_Idle;
}

static void NP_LazyInit() {
    if (NP_InitDone)
        return;
    NP_InitDone = true;

#ifdef NUTPUNCH_WINDOSE
    WSADATA bitch = {0};
    WSAStartup(MAKEWORD(2, 2), &bitch);
#endif

    srand(NutPunch_TimeNS());
    for (size_t i = 0; i < sizeof(NutPunch_PeerId); i++)
        NP_PeerId[i] = (char)('A' + rand() % 26);

    NP_ResetImpl();

    NutPunch_Log(".-------------------------------------------------------------.");
    NutPunch_Log("| For troubleshooting multiplayer connectivity, please visit: |");
    NutPunch_Log("|    https://github.com/Schwungus/nutpunch#troubleshooting    |");
    NutPunch_Log("'-------------------------------------------------------------'");

    NP_Trace("TRACE OK");
}

void NutPunch_Shutdown() {
    NutPunch_Disconnect();

#ifdef NUTPUNCH_WINDOSE
    WSACleanup();
#endif
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

static NutPunch_Field* NP_GetPeerFields(NutPunch_Peer peer) {
    if (NutPunch_IsOnline() && peer == NutPunch_LocalPeer())
        return NP_PeerMetadata;
    else if (peer >= 0 && peer < NUTPUNCH_MAX_PLAYERS)
        return NP_Peers[peer].metadata;
    else
        return NULL;
}

static size_t NP_GetVarCount(const NutPunch_Field* fields) {
    size_t count = 0;
    while (fields)
        count++, fields = fields->next;
    return count;
}

static const char* NP_GetVar(const NutPunch_Field* fields, const char* name) {
    if (!fields || !name)
        return NULL;

    for (const NutPunch_Field* ptr = fields; ptr; ptr = ptr->next)
        if (!NutPunch_StrNCmp(name, ptr->name, NUTPUNCH_FIELD_NAME_MAX))
            return ptr->data;

    return NULL;
}

static void NP_SetVar(NutPunch_Field** fields, const char* name, const char* data) {
    if (!fields || !name || !data)
        return;

    NutPunch_Field* target = *fields;

    for (; target; target = target->next)
        if (!NutPunch_StrNCmp(target->name, name, NUTPUNCH_FIELD_NAME_MAX))
            break;

    if (!target) {
        if (NP_GetVarCount(*fields) + 1 > NUTPUNCH_MAX_FIELDS) {
            NP_Warn("Can't add more than %d fields!", NUTPUNCH_MAX_FIELDS);
            return;
        }

        target = (NutPunch_Field*)NutPunch_Malloc(sizeof(*target));
        NP_MemzeroRef(*target);

        NutPunch_SNPrintF(target->name, sizeof(target->name), "%s", name);
        target->next = *fields, *fields = target;
    }

    NutPunch_SNPrintF(target->data, sizeof(target->data), "%s", data);
}

static int NP_DumpMetadata(void* raw_out, const NutPunch_Field* fields) {
    char* out = (char*)raw_out;

    for (; fields; fields = fields->next) {
        NutPunch_Memcpy(out, fields, sizeof(NP_Field));
        out += sizeof(NP_Field);
    }

    return (int)(out - (char*)raw_out);
}

void NutPunch_RequestLobbyData(const NutPunch_LobbyId lobby) {
    if (NP_Socket == NUTPUNCH_INVALID_SOCKET)
        return;

    static uint8_t buf[sizeof(NP_Header) + sizeof(NutPunch_LobbyId)] = "LIST";
    NutPunch_Memcpy(buf + sizeof(NP_Header), lobby, sizeof(NutPunch_LobbyId));

    NP_JustSend(NP_ServerAddr, buf, sizeof(buf), false);
}

const char* NutPunch_GetLobbyData(const char* name) {
    return NP_GetVar(NP_LobbyMetadata, name);
}

const char* NutPunch_GetPeerData(NutPunch_Peer peer, const char* name) {
    return NP_GetVar(NP_GetPeerFields(peer), name);
}

void NutPunch_SetLobbyData(const char* name, const char* data) {
    NP_SetVar(&NP_LobbyMetadata, name, data);
}

void NutPunch_SetPeerData(const char* name, const char* data) {
    NP_SetVar(&NP_PeerMetadata, name, data);
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
        goto fail;
    }

    if (!resolved) {
        NP_Warn("Couldn't resolve NutPuncher address");
        goto fail;
    }

    NP_MemzeroRef(NP_ServerAddr);
    NutPunch_Memcpy(&NP_ServerAddr, resolved->ai_addr, resolved->ai_addrlen);
    freeaddrinfo(resolved);

    NP_Info("Resolved NutPuncher address");
    return true;

fail:
    NP_LastStatus = NPS_Error;
    return false;
}

bool NP_MakeNonblocking(NP_Sock sock) {
#ifdef NUTPUNCH_WINDOSE
    u_long argp = 1;
    return !ioctlsocket(sock, FIONBIO, &argp);
#else
    return !fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
#endif
}

bool NP_MakeReuseAddr(NP_Sock sock) {
#ifdef NUTPUNCH_WINDOSE
    const u_long argp = 1;
#else
    const uint32_t argp = 1;
#endif
    const char* const shit = (const char*)&argp;
    return !setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, shit, sizeof(argp));
}

static bool NP_BindSocket() {
    NP_SockAddr local = {0};
    NP_LazyInit(), NP_NukeSocket(&NP_Socket);
    NP_Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (NP_Socket == NUTPUNCH_INVALID_SOCKET) {
        const int err = NP_SockError();
        NP_Warn("Failed to create the underlying UDP socket (%d)", err);
        goto sockfail;
    }

    if (!NP_MakeReuseAddr(NP_Socket)) {
        const int err = NP_SockError();
        NP_Warn("Failed to set socket reuseaddr option (%d)", err);
        goto sockfail;
    }

    if (!NP_MakeNonblocking(NP_Socket)) {
        const int err = NP_SockError();
        NP_Warn("Failed to set socket to non-blocking mode (%d)", err);
        goto sockfail;
    }

    local.sin_family = AF_INET;
    local.sin_port = htons(0);
    local.sin_addr.s_addr = htonl(INADDR_ANY);

    if (!bind(NP_Socket, (struct sockaddr*)&local, sizeof(local)))
        return true;

    NP_Warn("Failed to bind a UDP socket (%d)", NP_SockError());
sockfail:
    NutPunch_Reset();
    return false;
}

static bool NP_Connect(const char* lobby_id, bool sane, NP_HeartbeatFlagsStorage flags) {
    NP_LazyInit();

    if (flags & NP_HB_Queue)
        NP_NukeLobbyDataLite();
    else
        NP_NukeLobbyData();

    if (sane && (!lobby_id || !lobby_id[0])) {
        NP_Warn("Lobby ID cannot be null or empty!");
        NP_LastStatus = NPS_Error;
        return false;
    }

    if (!NP_BindSocket() || !NP_ResolveNutpuncher())
        return false;

    NP_HeartbeatFlags = flags;

    NP_Info("Ready to send heartbeats");
    NP_LastStatus = NPS_Online;
    NP_Memzero(NP_LastError);

    if (lobby_id)
        NutPunch_SNPrintF(NP_LobbyId, sizeof(NP_LobbyId), "%s", lobby_id);

    NP_LastBeating = NutPunch_TimeNS();

    return true;
}

bool NutPunch_QueryMode() {
    if (!NP_Connect(NULL, false, 0))
        return false;
    NP_Mode = NPNM_Query;
    return true;
}

bool NutPunch_Host(const char* lobby_id) {
    if (!NP_Connect(lobby_id, true, 0))
        return false;

    NP_Mode = NPNM_Normal;
    NP_Info("Hosting lobby '%s'", NP_LobbyId);

    return true;
}

bool NutPunch_Join(const char* lobby_id) {
    if (!NP_Connect(lobby_id, true, NP_HB_JoinExisting))
        return false;

    NP_Mode = NPNM_Normal;
    NP_Info("Joining lobby '%s'", NP_LobbyId);

    return true;
}

bool NutPunch_EnterQueue(const char* queue_id) {
    NP_LazyInit();

    if (!NP_Connect(NULL, false, 0))
        return false;

    NP_Mode = NPNM_Matchmaking;
    NP_Memzero(NP_QueueId);

    if (queue_id)
        NutPunch_SNPrintF(NP_QueueId, sizeof(NP_QueueId), "%s", queue_id);

    return true;
}

int NutPunch_QueueTime() {
    return NP_QueueTime;
}

int NutPunch_QueueCount() {
    return NP_QueueCount;
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
    if (players < 2 || players > NUTPUNCH_MAX_PLAYERS) {
        NP_Warn("Setting %d players max (requested %d)", NUTPUNCH_MAX_PLAYERS, players);
        players = NUTPUNCH_MAX_PLAYERS;
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
    if (NP_Socket == NUTPUNCH_INVALID_SOCKET)
        return;

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

    NP_JustSend(NP_ServerAddr, query, ptr - query, false);
}

const char* NutPunch_GetLastError() {
    return NP_LastError;
}

static void NP_HandleEventCb(NutPunch_CallbackEvent event, const void* data) {
    if (NP_Callbacks[event])
        NP_Callbacks[event](data);
}

static void NP_KillPeer(NutPunch_Peer peer) {
    if (peer >= NUTPUNCH_MAX_PLAYERS)
        return;

    if (NutPunch_PeerAlive(peer))
        NP_HandleEventCb(NPCB_PeerLeft, &peer);

    NP_NukePeer(peer);
}

bool NP_AddrNull(NP_SockAddr addr) {
    return !ntohl(addr.sin_addr.s_addr) && !ntohs(addr.sin_port);
}

bool NP_AddrEq(NP_SockAddr a, NP_SockAddr b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

static void NP_HandlePing(NP_Message msg) {
    const uint8_t* ptr = msg.data;

    const NutPunch_Peer idx = *ptr++;
    msg.len--;

    if (idx >= NUTPUNCH_MAX_PLAYERS)
        return;

    const bool was_dead = !NutPunch_PeerAlive(idx);

    NP_PeerInfo* const peer = &NP_Peers[idx];
    peer->address = msg.from, peer->last_ping = NutPunch_TimeNS();

    static NutPunch_PeerFieldDiff changed[NUTPUNCH_MAX_FIELDS] = {0};
    NP_Memzero(changed);

    size_t changed_count = 0;

    for (size_t i = 0; i < msg.len / sizeof(NP_Field); i++) {
        NP_Field now = ((NP_Field*)ptr)[i];

        for (NutPunch_Field* then = peer->metadata; then; then = then->next) {
            if (NutPunch_StrNCmp(then->name, now.name, NUTPUNCH_FIELD_NAME_MAX))
                continue;

            if (!NutPunch_StrNCmp(then->data, now.data, NUTPUNCH_FIELD_DATA_MAX))
                continue;

            NutPunch_PeerFieldDiff* diff = &changed[changed_count++];
            NutPunch_SNPrintF(diff->name, sizeof(diff->name), "%s", then->name);
            NutPunch_SNPrintF(diff->then, sizeof(diff->then), "%s", then->data);
            NutPunch_SNPrintF(diff->now, sizeof(diff->now), "%s", now.data);
            diff->peer = idx;

            break;
        }

        NP_SetVar(&peer->metadata, now.name, now.data);
    }

    if (was_dead)
        NP_HandleEventCb(NPCB_PeerJoined, &idx);

    // makes more sense to emit peer metadata changes AFTER they've been joined.
    for (size_t i = 0; i < changed_count; i++)
        NP_HandleEventCb(NPCB_PeerMetadataChanged, &changed[i]);
}

static void NP_HandleGTFO(NP_Message msg) {
    if (!NP_AddrEq(msg.from, NP_ServerAddr))
        return;

    switch (msg.data[0]) {
    case NPE_NoSuchLobby:
        NP_Warn("Lobby doesn't exist: '%s'", NP_LobbyId);
        break;
    case NPE_LobbyExists:
        NP_Warn("The lobby you're hosting exists: '%s'", NP_LobbyId);
        break;
    case NPE_LobbyFull:
        NP_Warn("Lobby '%s' is full!", NP_LobbyId);
        break;
    case NPE_QueueNoMatch:
        NP_Warn("We found no players to match you with!");
        break;
    case NPE_Sybau:
        NP_Warn("sybau :wilted_rose:");
        break;
    default:
        NP_Warn("Unidentified error");
        break;
    }

    NP_LastStatus = NPS_Error;
}

static void NP_PrintOurAddress(const uint8_t* data) {
    NP_SockAddr addr = {0};
    addr.sin_addr.s_addr = *(uint32_t*)data, data += 4;
    addr.sin_port = ntohs(*(uint16_t*)data), data += 2;
    NP_Info("Server thinks you are %s", NP_FormatSockaddr(addr));
}

static NutPunch_Peer NP_FindPeer(NP_SockAddr addr) {
    for (NutPunch_Peer i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
        if (NP_AddrEq(NP_Peers[i].address, addr))
            return i;
    return NUTPUNCH_MAX_PLAYERS;
}

static void NP_SendPings(int idx, const uint8_t* data) {
    if (NP_Socket == NUTPUNCH_INVALID_SOCKET || idx == NP_LocalPeer)
        return;

    NP_SockAddr pub = {0}, same_nat = {0};

    if (data) {
        pub.sin_family = AF_INET;
        pub.sin_addr.s_addr = *(uint32_t*)data, data += 4;
        pub.sin_port = *(uint16_t*)data, data += 2;

        same_nat.sin_family = AF_INET;
        same_nat.sin_addr.s_addr = *(uint32_t*)data, data += 4;
        same_nat.sin_port = *(uint16_t*)data, data += 2;
    }

    if (NP_AddrNull(pub) && NP_AddrNull(same_nat)) { // they're dead on the NutPuncher's side
        NP_KillPeer(idx);
        return;
    }

    NP_PeerInfo* const peer = &NP_Peers[idx];

    static uint8_t ping[sizeof(NP_Header) + 1 + sizeof(NP_Metadata)] = "PING";
    uint8_t* ptr = &ping[sizeof(NP_Header)];

    *ptr++ = NP_LocalPeer;
    ptr += NP_DumpMetadata(ptr, NP_PeerMetadata);

    NP_JustSend(pub, ping, ptr - ping, false);
    NP_JustSend(same_nat, ping, ptr - ping, false);
}

static void NP_HandleBeating(NP_Message msg) {
    if (!NP_AddrEq(msg.from, NP_ServerAddr))
        return;

    NP_Trace("GOT BEATEN!!!");

    const bool just_joined = NutPunch_LocalPeer() == NUTPUNCH_MAX_PLAYERS;
    const NutPunch_Peer old_master = NutPunch_MasterPeer();
    const uint8_t* ptr = msg.data;

    NP_Unlisted = *ptr++;
    NP_LocalPeer = *ptr++;
    NP_Master = *ptr++;
    const size_t num_peers = *ptr++;
    NP_MaxPlayers = *ptr++;

    if (NP_LocalPeer >= NUTPUNCH_MAX_PLAYERS) {
        NP_LocalPeer = NUTPUNCH_MAX_PLAYERS;
        NP_Warn("NutPuncher sent us a junk response?!");
        return;
    }

    const NutPunch_Peer new_master = NutPunch_MasterPeer();

    // add the join-existing flag after a successful join because otherwise we could get a
    // random-ass disconnection with the "Lobby already exists!" error message.
    NP_HeartbeatFlags |= NP_HB_JoinExisting;

    // sync outgoing max player count with the one we just received.
    NP_HeartbeatFlags &= 0xF;
    NP_HeartbeatFlags |= (NutPunch_GetMaxPlayers() - 1) << 4;

    const size_t expected_len = num_peers * (1 + 2 * sizeof(NP_PeerAddr));
    msg.len -= sizeof(NP_Beating);

    if (msg.len < expected_len) {
        NP_Warn("Bad peer data in beating");
        goto done_getting_beat;
    }

    static const uint8_t* addrs[NUTPUNCH_MAX_PLAYERS] = {0};
    NutPunch_Memset((void*)addrs, 0, sizeof(addrs));
    for (int i = 0; i < num_peers; i++) {
        const NutPunch_Peer pidx = *ptr++;
        addrs[pidx] = ptr;
        ptr += 2 * sizeof(NP_PeerAddr);
    }

    for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
        NP_SendPings(i, addrs[i]);
        if (i == NP_LocalPeer && just_joined && addrs[i])
            NP_PrintOurAddress(addrs[i]);
    }

    for (size_t i = 0; i < msg.len / sizeof(NP_Field); i++) {
        NP_Field now = ((NP_Field*)ptr)[i];

        for (NutPunch_Field* then = NP_LobbyMetadata; then; then = then->next) {
            if (NutPunch_StrNCmp(then->name, now.name, NUTPUNCH_FIELD_NAME_MAX))
                continue;

            if (NutPunch_StrNCmp(then->data, now.data, NUTPUNCH_FIELD_DATA_MAX))
                continue;

            NutPunch_FieldDiff diff = {0};
            NutPunch_SNPrintF(diff.name, sizeof(diff.name), "%s", then->name);
            NutPunch_SNPrintF(diff.then, sizeof(diff.then), "%s", then->data);
            NutPunch_SNPrintF(diff.now, sizeof(diff.now), "%s", now.data);
            NP_HandleEventCb(NPCB_LobbyMetadataChanged, &diff);

            break;
        }

        NP_SetVar(&NP_LobbyMetadata, now.name, now.data);
    }

done_getting_beat:
    if (old_master != new_master) {
        if (new_master == NutPunch_LocalPeer())
            NP_Info("We're the lobby's master now");
        NP_HandleEventCb(NPCB_NewMaster, &new_master);
    }

    NP_LastBeating = NutPunch_TimeNS();
}

static void NP_HandleListing(NP_Message msg) {
    if (!NP_AddrEq(msg.from, NP_ServerAddr))
        return;

    NutPunch_LobbyList list = {0};
    list.count = msg.len / sizeof(NutPunch_LobbyInfo);
    list.lobbies = list.count ? (NutPunch_LobbyInfo*)msg.data : NULL;

    NP_HandleEventCb(NPCB_LobbyList, &list);
}

static void NP_HandleLobbyData(NP_Message msg) {
    if (!NP_AddrEq(msg.from, NP_ServerAddr))
        return;

    NutPunch_LobbyMetadata info = {0};

    NutPunch_Memcpy(info.lobby, msg.data, sizeof(info.lobby));
    msg.data += sizeof(NutPunch_LobbyId);
    msg.len -= sizeof(NutPunch_LobbyId);

    for (size_t i = 0; i < (msg.len / sizeof(NP_Field)); i++) {
        NutPunch_Field* field = (NutPunch_Field*)NutPunch_Malloc(sizeof(*field));
        field->next = info.metadata, info.metadata = field;
        NutPunch_Memcpy(field, msg.data, sizeof(NP_Field)), msg.data += sizeof(NP_Field);
    }

    NP_HandleEventCb(NPCB_LobbyMetadata, &info);

    NP_NukeMetadata(&info.metadata);
}

static void NP_HandleData(NP_Message msg) {
    if (NP_AddrEq(msg.from, NP_ServerAddr))
        return;

    NutPunch_Peer peer_idx = NP_FindPeer(msg.from);

    if (peer_idx == NUTPUNCH_MAX_PLAYERS)
        return;

    const NutPunch_Channel chan = (msg.len--, *msg.data++);
    if (chan >= NP_ChannelCount)
        return;

    NP_PeerInfo* const peer = &NP_Peers[peer_idx];

    NP_IncomingData* last = NULL;
    for (; last && last->next; last = last->next) {}

    last = *(last ? &last->next : &NP_Unread[chan])
        = (NP_IncomingData*)NutPunch_Malloc(sizeof(NP_IncomingData));

    last->peer = peer_idx, last->len = msg.len, last->next = NULL;
    last->data = NutPunch_Malloc(last->len);
    NutPunch_Memcpy(last->data, msg.data, last->len);
}

static void NP_HandleQueue(NP_Message msg) {
    if (NP_AddrEq(msg.from, NP_ServerAddr)) {
        NP_QueueTime = *msg.data++;
        NP_QueueCount = *msg.data++;
    }
}

static void NP_HandleDate(NP_Message msg) {
    if (NP_Mode == NPNM_Matchmaking && NP_AddrEq(msg.from, NP_ServerAddr)) {
        NP_Connect((char*)msg.data, true, NP_HB_Queue);
        NP_HandleEventCb(NPCB_QueueCompleted, msg.data);
    }
}

static void NP_HandleAcky(NP_Message msg) {
    for (NP_OutgoingPacket* cur = NP_Pending; cur; cur = cur->next)
        cur->acked |= (cur->id == ntohl(*(uint32_t*)msg.data));
}

static void NP_SendHeartbeat() {
    if (NP_Socket == NUTPUNCH_INVALID_SOCKET)
        return;

    static uint8_t heartbeat[sizeof(NP_Header) + sizeof(NP_Heartbeat) + sizeof(NP_Metadata)] = {0};
    NP_Memzero(heartbeat);

    uint8_t* ptr = heartbeat;

    NP_SockAddr addr = {0};
    socklen_t addr_size = sizeof(addr);
    getsockname(NP_Socket, (struct sockaddr*)&addr, &addr_size);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    switch (NP_Mode) {
    case NPNM_Normal:
        NutPunch_Memcpy(ptr, "JOIN", sizeof(NP_Header));
        ptr += sizeof(NP_Header);

        NutPunch_Memcpy(ptr, NP_PeerId, sizeof(NutPunch_PeerId));
        ptr += sizeof(NutPunch_PeerId);

        NutPunch_Memcpy(ptr, NP_LobbyId, sizeof(NutPunch_LobbyId));
        ptr += sizeof(NutPunch_LobbyId);

        *(NP_HeartbeatFlagsStorage*)ptr = NP_HeartbeatFlags,
        ptr += sizeof(NP_HeartbeatFlagsStorage);

        *(uint32_t*)ptr = addr.sin_addr.s_addr, ptr += 4;
        *(uint16_t*)ptr = addr.sin_port, ptr += 2;

        ptr += NP_DumpMetadata(ptr, NP_LobbyMetadata);

        break;

    case NPNM_Matchmaking:
        NutPunch_Memcpy(ptr, "FIND", sizeof(NP_Header));
        ptr += sizeof(NP_Header);

        NutPunch_Memcpy(ptr, NP_PeerId, sizeof(NutPunch_PeerId));
        ptr += sizeof(NutPunch_PeerId);

        NutPunch_Memcpy(ptr, NP_QueueId, sizeof(NutPunch_QueueId));
        ptr += sizeof(NutPunch_QueueId);

        break;

    default:
        return;
    }

    NP_JustSend(NP_ServerAddr, heartbeat, ptr - heartbeat, false);
}

static void NP_FlushPendingQueue() {
    if (NP_Socket == NUTPUNCH_INVALID_SOCKET)
        return;

    const NutPunch_Clock now = NutPunch_TimeNS();

    for (NP_OutgoingPacket *prev = NULL, *cur = NP_Pending; cur; cur = cur->next) {
        bool send = false, nuke = false;

        if (cur->retries < 0) {
            send = nuke = true;
        } else if (cur->last_retry) {
            const bool due = now - cur->last_retry
                             > (NUTPUNCH_RETRY_INTERVAL * (cur->retries + 1)) * NUTPUNCH_MS;
            nuke = cur->acked || due && cur->retries++ > NUTPUNCH_MAX_RETRIES;
            send = due && !nuke;
        } else {
            send = true, nuke = false;
        }

        if (send) {
            cur->last_retry = now;

            struct sockaddr* dest = (struct sockaddr*)&cur->destination;
            sendto(NP_Socket, (char*)cur->data, cur->len, 0, dest, sizeof(cur->destination));
        }

        if (nuke) {
            *(prev ? &prev->next : &NP_Pending) = cur->next;
            NutPunch_Free(cur->data), NutPunch_Free(cur);
        } else {
            prev = cur;
        }
    }
}

static int NP_UglyRecvFrom(NP_SockAddr* addr, void* buf, int buf_size) {
    socklen_t addr_size = sizeof(*addr);
    struct sockaddr* const shit_addr = (struct sockaddr*)addr;
    return recvfrom(NP_Socket, (char*)buf, buf_size, 0, shit_addr, &addr_size);
}

static void NP_ReceiveShit() {
    const int prefix = 4;

    for (;;) {
        NP_SockAddr addr = {0};
        static uint8_t buf[NUTPUNCH_FRAGMENT_SIZE] = {0};
        int size = NP_UglyRecvFrom(&addr, buf, sizeof(buf));

        if (size < 0) {
            if (NP_SockError() == NP_WouldBlock) {
                break;
            } else if (NP_SockError() == NP_TooFat || NP_SockError() == NP_ConnReset) {
                continue;
            } else {
                NutPunch_Reset();
                NP_Warn("Things went haywire while receiving: %d", NP_SockError());
                NP_LastStatus = NPS_Error;
                break;
            }
        }

        NP_Trace("RECEIVED %i BYTES FROM %s", size, NP_FormatSockaddr(addr));
        size -= prefix + (int)sizeof(NP_Header);

        if (size < 0)
            continue; // junk

        uint32_t id = ntohl(*(uint32_t*)buf);
        if (id) { // ackies
            static uint8_t acky[sizeof(NP_Header) + 4] = "ACKY";
            *(uint32_t*)(acky + sizeof(NP_Header)) = htonl(id);
            NP_JustSpam(addr, acky, sizeof(acky));
        }

        for (size_t i = 0; i < sizeof(NP_MessageTypes) / sizeof(*NP_MessageTypes); i++) {
            const NP_MessageType type = NP_MessageTypes[i];

            if (size < type.min_packet_size)
                continue;
            if (NutPunch_Memcmp(buf + prefix, type.identifier, sizeof(NP_Header)))
                continue;

            NP_Message msg = {0};
            msg.from = addr, msg.len = size;
            msg.data = (uint8_t*)(buf + prefix + sizeof(NP_Header));
            type.handle(msg);

            break;
        }
    }
}

static void NP_SendGoodbyes() {
    if (NP_Socket == NUTPUNCH_INVALID_SOCKET)
        return;

    if (NP_Mode == NPNM_Query)
        return;

    static uint8_t bye[sizeof(NP_Header) + sizeof(NutPunch_PeerId)] = "DISC";
    NutPunch_Memcpy(bye + sizeof(NP_Header), NP_PeerId, sizeof(NutPunch_PeerId));
    NP_JustSpam(NP_ServerAddr, bye, sizeof(bye));
}

void NutPunch_Flush() {
    if (NP_Closing)
        NP_SendGoodbyes();
    NP_FlushPendingQueue();
}

void NutPunch_Register(NutPunch_CallbackEvent event, NutPunch_Callback cb) {
    if (event < NPCB_Count)
        NP_Callbacks[event] = cb;
}

static void NP_NetworkUpdate() {
    NP_SendHeartbeat();
    NP_ReceiveShit();
    NP_FlushPendingQueue();

    const NutPunch_Clock now = NutPunch_TimeNS();

    for (NutPunch_Peer i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
        if (i != NutPunch_LocalPeer() && NutPunch_PeerAlive(i)
            && now - NP_Peers[i].last_ping >= NUTPUNCH_TIMEOUT_INTERVAL * NUTPUNCH_MS)
        {
            NP_KillPeer(i);
        }
    }

    if (NP_Mode != NPNM_Query) {
        if (now - NP_LastBeating >= NUTPUNCH_TIMEOUT_INTERVAL * NUTPUNCH_MS) {
            NP_Warn("NutPuncher timed out");
            NP_LastStatus = NPS_Error;
        }
    }
}

NutPunch_UpdateStatus NutPunch_Update() {
    NP_LazyInit();

    if (NP_LastStatus == NPS_Idle || NP_Socket == NUTPUNCH_INVALID_SOCKET)
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
        NP_Closing = true, NutPunch_Flush();
    NutPunch_Reset();
}

void NutPunch_SetChannelCount(int count) {
    if (count < 1 || count > NUTPUNCH_MAX_CHANNELS)
        return;
    NP_ChannelCount = count;
}

bool NutPunch_HasMessage(NutPunch_Channel chan) {
    return chan < NUTPUNCH_MAX_CHANNELS && NP_Unread[chan] != NULL;
}

int NutPunch_NextMessage(NutPunch_Channel chan, void* out, int* size) {
    if (chan >= NUTPUNCH_MAX_CHANNELS) {
        NP_Warn("We don't have %d channels bro", chan + 1);
        return NUTPUNCH_MAX_PLAYERS;
    }

    NP_IncomingData* const packet = NP_Unread[chan];

    if (!packet) {
        NP_Warn("You forgot to check `NutPunch_HasMessage(%d)`", chan);
        return NUTPUNCH_MAX_PLAYERS;
    }

    if (size && *size < packet->len) {
        NP_Warn("Not enough memory allocated to copy the next packet");
        return NUTPUNCH_MAX_PLAYERS;
    }

    if (size)
        *size = (int)packet->len;
    if (out)
        NutPunch_Memcpy(out, packet->data, packet->len);

    NP_Unread[chan] = packet->next;
    NutPunch_Free(packet->data);
    NutPunch_Free(packet);

    if (packet->peer >= NUTPUNCH_MAX_PLAYERS)
        return NUTPUNCH_MAX_PLAYERS;
    return packet->peer;
}

static void NP_SendPro(
    NutPunch_Channel channel, NutPunch_Peer peer, const void* data, int size, bool reliable) {
    if (!NutPunch_PeerAlive(peer) || NutPunch_LocalPeer() == peer || size <= 0
        || channel >= NUTPUNCH_MAX_CHANNELS || channel >= NP_ChannelCount)
    {
        return;
    }

    const size_t total_size = sizeof(NP_Header) + 1 + size;
    uint8_t* const buf = (uint8_t*)NutPunch_Malloc(total_size);
    uint8_t* ptr = buf + sizeof(NP_Header);

    NutPunch_Memcpy(buf, "DATA", sizeof(NP_Header));
    *ptr++ = channel, NutPunch_Memcpy(ptr, data, size);
    NP_JustSend(NP_Peers[peer].address, buf, total_size, reliable);

    NutPunch_Free(buf);
}

void NutPunch_Send(NutPunch_Channel channel, NutPunch_Peer peer, const void* data, int size) {
    NP_SendPro(channel, peer, data, size, false);
}

void
NutPunch_SendReliably(NutPunch_Channel channel, NutPunch_Peer peer, const void* data, int size) {
    NP_SendPro(channel, peer, data, size, true);
}

int NutPunch_PeerCount() {
    int count = 0;
    for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
        count += NutPunch_PeerAlive(i);
    return count;
}

bool NutPunch_PeerAlive(NutPunch_Peer peer) {
    if (peer >= NUTPUNCH_MAX_PLAYERS)
        return false;
    if (NutPunch_LocalPeer() == peer)
        return true;
    return !NP_AddrNull(NP_Peers[peer].address);
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
    size_t len = 0;

    while (path[len])
        len++;

    for (size_t i = len - 2; i >= 0; i--)
        if (path[i] == '/' || path[i] == '\\')
            return &path[i + 1];

    return path;
}

// internally used `Sleep` polyfill. we only really use it in the NutPuncher & test binaries.
#if !defined(NUTPUNCH_WINDOSE) && !defined(NUTPUNCH_NOSTD)

#include <errno.h>
#include <time.h>

void NP_SleepMs(int ms) {
    // Stolen from: <https://stackoverflow.com/a/1157217>
    struct timespec ts = {0};
    ts.tv_sec = ms / 1000, ts.tv_nsec = (ms % 1000) * NUTPUNCH_MS;
    int res = 0;
    do { res = nanosleep(&ts, &ts); } while (res && errno == EINTR);
}

#endif // NP_SleepMs

#if !defined(NUTPUNCH_NOSTD) && !defined(NutPunch_TimeNS)

#include <time.h>

NutPunch_Clock NutPunch_TimeNS() {
    struct timespec ts = {0};
    clock_gettime(CLOCK_REALTIME, &ts);
    return (NutPunch_Clock)ts.tv_sec * NUTPUNCH_SEC + (NutPunch_Clock)ts.tv_nsec;
}

#elif defined(NUTPUNCH_NOSTD) && !defined(NutPunch_TimeNS)

#error You have to define NutPunch_TimeNS in order to use NUTPUNCH_NOSTD!

#endif // NutPunch_TimeNS

#endif // NUTPUNCH_IMPLEMENTATION

#ifdef __cplusplus
}
#endif

#endif // NUTPUNCH_H
