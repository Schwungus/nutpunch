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

#include <enet/enet.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(_WIN64)
#define NUTPUNCH_WINDOSE
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

/// The maximum amount of channels a NutPunch host can send to/receive on.
#define NUTPUNCH_MAX_CHANNELS (30)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h> // IWYU pragma: export

/// Set this pointer to NULL to use the default NutPunch logger, or override the NutPunch logger by
/// supplying your own logging function.
///
/// NutPunch breaks its logging output into lines for you. On the most basic level, all you need to
/// do to implement a logger is to supply a varargs `printf` implementation.
extern void (*NP_Logger)(const char*, ...);

void NP_DefaultLogger(const char*, ...);

extern char NP_LastError[512];

#define NutPunch_Log(level, fmt, ...)                                                              \
    ((NP_Logger ? NP_Logger : NP_DefaultLogger)(                                                   \
        level " (%s:%d) " fmt "\n", NutPunch_Basename(__FILE__), __LINE__, ##__VA_ARGS__))

#define NutPunch_SNPrintF snprintf

#define NutPunch_Malloc malloc
#define NutPunch_Free free

#define NutPunch_Memcpy memcpy
#define NutPunch_Memset memset
#define NutPunch_Memcmp memcmp

#define NP_Memzero(array) NutPunch_Memset(array, 0, sizeof(array))
#define NP_MemzeroRef(ref) NutPunch_Memset(&(ref), 0, sizeof(ref))

#define NP_Info(...) NutPunch_Log("INFO", __VA_ARGS__)
#define NP_Warn(...)                                                                               \
    do {                                                                                           \
        NutPunch_Log("WARN", __VA_ARGS__);                                                         \
        NutPunch_SNPrintF(NP_LastError, sizeof(NP_LastError), ##__VA_ARGS__);                      \
    } while (0)

#ifdef NUTPUNCH_WINDOSE
#define NP_SleepMs(ms) Sleep(ms)
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

typedef enum {
    /// Set this flag to send a reliable packet. This means the packet will be periodically resent
    /// until delivery is confirmed by the target peer.
    NP_Send_Reliably = ENET_PACKET_FLAG_RELIABLE,

    /// Set this flag to disable the default behavior of sequencing every non-reliable packet.
    ///
    /// Sequencing ensures the packets arrive in the order they were sent and discards them
    /// otherwise.
    NP_Send_Unsequenced = ENET_PACKET_FLAG_UNSEQUENCED,
} NutPunch_SendFlags;

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

/// Sends data on the specified channel, to the specified peer, with the specified flags. Use 0 or
/// bitwise-or one or more `NP_Send_*` constants to assemble the flags argument.
void NutPunch_Send(NutPunch_Channel, NutPunch_Peer, uint32_t, const void*, int);

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
NutPunch_Clock NutPunch_TimeNS();

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

typedef struct {
    uint32_t ip;
    uint16_t port;
} NP_PeerAddr;

typedef struct {
    uint8_t unlisted;
    NutPunch_Peer local, master, capacity;
    NP_PeerAddr peers[2][NUTPUNCH_MAX_PLAYERS];
    NutPunch_Metadata metadata;
} NP_Beating;

typedef struct {
    NutPunch_PeerId peer;
    NutPunch_LobbyId lobby;
    NP_HeartbeatFlagsStorage flags;
    NP_PeerAddr same_nat;
    NutPunch_Metadata lobby_metadata;
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

#ifdef __cplusplus
}
#endif

#endif // NUTPUNCH_H
