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
#define NUTPUNCH_MAX_PLAYERS (16)

#ifndef NUTPUNCH_SERVER_PORT
/// The UDP port used by the punching mediator server.
#define NUTPUNCH_SERVER_PORT (30000)
#endif

#ifndef NUTPUNCH_SERVER_TIMEOUT_SECS
/// How many seconds to wait for NutPuncher to respond before disconnecting.
#define NUTPUNCH_SERVER_TIMEOUT_SECS (10)
#endif

#ifndef NUTPUNCH_PEER_TIMEOUT_SECS
/// How many seconds to wait for a peer to respond before timing out.
#define NUTPUNCH_PEER_TIMEOUT_SECS (10)
#endif

/// How many bytes to reserve for every network packet.
#define NUTPUNCH_BUFFER_SIZE (8192)

/// The maximum amount of results `NutPunch_LobbyList` can provide.
#define NUTPUNCH_MAX_SEARCH_RESULTS (32)

/// The maximum amount of filters you can pass to `NutPunch_FindLobbies`.
#define NUTPUNCH_MAX_SEARCH_FILTERS (8)

/// Maximum length of a metadata field name.
#define NUTPUNCH_FIELD_NAME_MAX (8)

/// Maximum volume of data you can store in a metadata field.
#define NUTPUNCH_FIELD_DATA_MAX (16)

/// Maximum amount of metadata fields per lobby/player.
#define NUTPUNCH_MAX_FIELDS (12)

/// How many updates to wait before resending a reliable packet.
#define NUTPUNCH_BOUNCE_TICKS (20)

#define NUTPUNCH_PORT_MIN ((uint16_t)(NUTPUNCH_SERVER_PORT + 1))
#define NUTPUNCH_PORT_MAX ((uint16_t)(NUTPUNCH_PORT_MIN + 512))

#define NUTPUNCH_ADDRESS_SIZE (6)

#ifndef NUTPUNCH_NOSTD
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

// we still depend on `time.h` through `clock_t` and `clock()` though.
#include <time.h>

typedef char NutPunch_Id[32];

typedef struct {
	char name[NUTPUNCH_FIELD_NAME_MAX];
	char data[NUTPUNCH_FIELD_DATA_MAX];
	uint8_t size;
} NutPunch_Field;

typedef enum {
	NPSF_Players = 1,
	NPSF_Capacity,
} NutPunch_SpecialField;

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

typedef struct {
	char name[sizeof(NutPunch_Id) + 1];
	int players, capacity;
} NutPunch_LobbyInfo;

typedef enum {
	NPS_Error,
	NPS_Idle,
	NPS_Online,
} NutPunch_UpdateStatus;

typedef enum {
	NPF_Not = 1 << 0,
	NPF_Eq = 1 << 1,
	NPF_Less = 1 << 2,
	NPF_Greater = 1 << 3,
} NutPunch_Operator;

typedef enum {
	NPE_Ok,
	NPE_Sybau,
	NPE_NoSuchLobby,
	NPE_LobbyExists,
	NPE_LobbyFull,
	NPE_Max,
} NutPunch_ErrorCode;

/// Set a custom NutPuncher server address.
void NutPunch_SetServerAddr(const char* hostname);

/// Join a lobby by its ID. Return `false` if a network error occurs and `true`
/// otherwise.
///
/// If no lobby exists with this ID, an error status spits out of
/// `NutPunch_Update()` rather than immediately here.
bool NutPunch_Join(const char* lobby_id);

/// Host a lobby with the specified ID and maximum player count. Return `false`
/// if a network error occurs and `true` otherwise.
///
/// If the lobby with the same ID exists, an error status spits out of
/// `NutPunch_Update()` rather than immediately here.
bool NutPunch_Host(const char* lobby_id, int players);

/// Change the maximum player count after calling `NutPunch_Host`.
void NutPunch_SetMaxPlayers(int players);

/// Get the maximum player count of the lobby you are in. Returns 0 if you
/// aren't in a lobby.
int NutPunch_GetMaxPlayers();

/// Call this at the end of your program to disconnect gracefully and run other
/// semi-important cleanup.
void NutPunch_Cleanup();

/// Call this every frame to update nutpunch. Returns one of the `NPS_*`
/// constants you need to match against to see if something goes wrong.
NutPunch_UpdateStatus NutPunch_Update();

/// Request a field of lobby metadata to be set. Can be called multiple times in
/// a row to set multiple fields. This will send out metadata changes only after
/// a call to `NutPunch_Update()`, and won't do anything unless you're the
/// lobby's master.
///
/// See `NUTPUNCH_FIELD_NAME_MAX` and `NUTPUNCH_FIELD_DATA_MAX` for the amount
/// of data you can squeeze into a field.
void NutPunch_LobbySet(const char* name, int size, const void* data);

/// Query lobby metadata. Sets `size` to the field's actual size unless you
/// specify `NULL`. Metadata is updated with each call to `NutPunch_Update()`.
///
/// The resulting pointer is actually a static allocation, so don't rely too
/// much on it; its data will change after the next `NutPunch_LobbyGet` call.
///
/// You cannot query the metadata of a lobby you aren't connected to. To do
/// that, use `NutPunch_FindLobbies` and specify some filters.
const void* NutPunch_LobbyGet(const char* name, int* size);

/// Request your peer-specific metadata to be set. Otherwise, this works the
/// same way as `NutPunch_LobbySet`, which see.
void NutPunch_PeerSet(const char* name, int size, const void* data);

/// Query metadata for a specific peer. Otherwise, this works the same way as
/// `NutPunch_LobbyGet`, which see.
const void* NutPunch_PeerGet(int peer, const char* name, int* size);

/// Check if there is a packet waiting in the receiving queue. Retrieve it with
/// `NutPunch_NextMessage()`.
bool NutPunch_HasMessage();

/// Retrieve the next packet in the receiving queue. Reads up to `size` bytes
/// into `out`. Returns the index of the peer who sent it.
///
/// In case of an error, logs it and returns `NUTPUNCH_MAX_PLAYERS`. You can
/// retrieve a human-readable message later with `NutPunch_GetLastError()`.
///
/// `size` must be set to the output buffer's size. Passing an output buffer
/// that is smaller than `size` will crash your entire program.
int NutPunch_NextMessage(void* out, int* size);

/// Send data to specified peer. Copies the data into a dynamically allocated
/// buffer of `size` bytes.
///
/// For reliable packet delivery, try `NutPunch_SendReliably`.
void NutPunch_Send(int peer, const void* data, int size);

/// Send data to specified peer expecting them to acknowledge the fact of
/// reception. See the docs of `NutPunch_Send` for more usage notes.
void NutPunch_SendReliably(int peer, const void* data, int size);

/// Count how many "live" peers we have a route to, including our local peer.
///
/// Do not use this as an upper bound for iterating over peers. Iterate from 0
/// to `NUTPUNCH_MAX_PLAYERS` and check each peer individually with
/// `NutPunch_PeerAlive`.
int NutPunch_PeerCount();

/// Return true if you are connected to the peer with the specified index.
///
/// Use `NUTPUNCH_MAX_PLAYERS` as the upper bound for iterating, and check each
/// peer's status individually using this function.
bool NutPunch_PeerAlive(int peer);

/// Get the local peer's index. Returns `NUTPUNCH_MAX_PLAYERS` if this fails for
/// any reason.
int NutPunch_LocalPeer();

/// Returns `true` if we are in a lobby. This doesn't guarantee the local peer
/// index or any metadata is available; for that, see `NutPunch_IsReady()`.
bool NutPunch_IsOnline();

/// Returns `true` if we are in a lobby AND are ready to accept P2P connections.
/// Lobby metadata and local peer index will be available by this point.
bool NutPunch_IsReady();

/// Returns `true` if we are `NutPunch_IsReady()` AND are the lobby's master.
bool NutPunch_IsMaster();

/// Call this to gracefully disconnect from a lobby.
void NutPunch_Disconnect();

/// Query the lobbies list given a set of filters. Use `NutPunch_GetLobby` to
/// retrieve the results.
///
/// The list of lobbies will update after a few ticks of `NutPunch_Update`.
/// Don't expect immediate results.
///
/// Each filter consists of either a special or a named metadata field, with a
/// corresponding value to compare it to. All filters must match in order for a
/// lobby to be listed.
///
/// Bitwise-or the `NPF_*` constants to set a filter's comparison flags.
///
/// To query "special" fields such as lobby capacity, use one of the `NPSF_*`
/// constants with an `int8_t` value to compare it against. For example, to
/// query lobbies for exactly a 2-player duo to play:
///
/// ```
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
/// To query metadata fields, `memcpy` their names and values into the filter's
/// `field` property. The comparison will be performed bytewise in a fashion
/// similar to `memcmp`.
void NutPunch_FindLobbies(int filter_count, const NutPunch_Filter* filters);

/// Extract lobby info after a call to `NutPunch_FindLobbies`. Updates every
/// call to `NutPunch_Update`; don't expect an immediate response.
const NutPunch_LobbyInfo* NutPunch_GetLobby(int index);

/// Count how many lobbies were found after the call to
/// `NutPunch_FindLobbies`. Updates every call to `NutPunch_Update`.
int NutPunch_LobbyCount();

/// Call this to reset the underlying socket in case of an inexplicable error.
void NutPunch_Reset();

/// Get the human-readable description of the latest error in
/// `NutPunch_Update()` and a few other functions.
const char* NutPunch_GetLastError();

/// Return a substring of `path` without its directory name. A utility function
/// used internally in the default implementation of `NutPunch_Log`.
const char* NutPunch_Basename(const char* path);

#ifndef NutPunch_Log
#include <stdio.h>
#define NutPunch_Log(msg, ...)                                                 \
	do {                                                                   \
		fprintf(stdout, "(%s:%d) " msg "\n",                           \
			NutPunch_Basename(__FILE__), __LINE__, ##__VA_ARGS__); \
		fflush(stdout);                                                \
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

#ifndef NutPunch_Malloc
#include <stdlib.h>
#define NutPunch_Malloc malloc
#endif

#ifndef NutPunch_Free
#include <stdlib.h>
#define NutPunch_Free free
#endif

#ifndef NutPunch_Memcpy
#include <string.h>
#define NutPunch_Memcpy memcpy
#endif

#ifndef NutPunch_Memset
#include <string.h>
#define NutPunch_Memset memset
#endif

#ifndef NutPunch_Memcmp
#include <string.h>
#define NutPunch_Memcmp memcmp
#endif

#define NP_Memzero(array) NutPunch_Memset(array, 0, sizeof(array))
#define NP_MemzeroRef(ref) NutPunch_Memset(&(ref), 0, sizeof(ref))

#define NP_Info(...) NutPunch_Log("INFO: " __VA_ARGS__)
#define NP_Warn(...)                                                           \
	do {                                                                   \
		NutPunch_Log("WARN: " __VA_ARGS__);                            \
		NutPunch_SNPrintF(                                             \
			NP_LastError, sizeof(NP_LastError), __VA_ARGS__);      \
	} while (0)

#ifdef NUTPUNCH_TRACING
#define NP_Trace(...) NutPunch_Log("TRACE: " __VA_ARGS__)
#else // clang-format off
#define NP_Trace(...) do {} while (0)
#endif // clang-format on

typedef uint32_t NP_PacketIdx;
typedef struct sockaddr_in NP_Addr;
typedef uint8_t NP_HeartbeatFlagsStorage, NP_ResponseFlagsStorage;

typedef uint8_t NP_Header[4];
typedef NutPunch_Field NP_Metadata[NUTPUNCH_MAX_FIELDS];

typedef struct NP_Data {
	char* data;
	struct NP_Data* next;
	NP_PacketIdx index;
	uint32_t size;
	uint8_t peer, dead;
	int16_t bounce;
} NP_Data;

typedef struct {
	NP_Addr addr;
	clock_t last_beating, first_shalom;

	NP_PacketIdx recv_counter, send_counter;
	NP_Data* out_of_order;

	NP_Metadata metadata;
} NP_Peer;

typedef struct {
	NP_Addr addr;
	int size;
	const uint8_t* data;
} NP_Message;

typedef struct {
	uint8_t local_peer;
	NP_ResponseFlagsStorage flags;
	struct {
		uint8_t ip[4];
		uint16_t port;
	} peers[NUTPUNCH_MAX_PLAYERS];
	NP_Metadata metadata;
} NP_Beating;

typedef struct {
	NutPunch_LobbyInfo lobbies[NUTPUNCH_MAX_SEARCH_RESULTS];
} NP_Listing;

typedef struct {
	NutPunch_Id id;
	NP_HeartbeatFlagsStorage flags;
	NP_Metadata lobby_metadata;
} NP_Heartbeat;

typedef struct {
	const char identifier[sizeof(NP_Header) + 1];
	const int16_t packet_size;
	void (*const handler)(NP_Message);
} NP_MessageType;

#define NP_ANY_LEN (-1)

static void NP_HandleShalom(NP_Message), NP_HandleGTFO(NP_Message),
	NP_HandleBeating(NP_Message), NP_HandleListing(NP_Message),
	NP_HandleAcky(NP_Message), NP_HandleData(NP_Message);

// clang-format off
static const NP_MessageType NP_MessageTypes[] = {
	{"SHLM", 1 + sizeof(NP_Metadata), NP_HandleShalom },
	{"LIST", sizeof(NP_Listing),      NP_HandleListing},
	{"ACKY", sizeof(NP_PacketIdx),    NP_HandleAcky   },
	{"DATA", NP_ANY_LEN,              NP_HandleData   },
	{"GTFO", 1,		          NP_HandleGTFO   },
	{"BEAT", sizeof(NP_Beating),      NP_HandleBeating},
};
// clang-format on

static char NP_LastError[512] = "";
static clock_t NP_LastBeating = 0;

static bool NP_InitDone = false, NP_Closing = false;
static NutPunch_UpdateStatus NP_LastStatus = NPS_Idle;

static char NP_LobbyId[sizeof(NutPunch_Id) + 1] = {0};
static NP_Peer NP_Peers[NUTPUNCH_MAX_PLAYERS] = {0};
static uint8_t NP_LocalPeer = NUTPUNCH_MAX_PLAYERS;

static NP_Socket NP_Sock = NUTPUNCH_INVALID_SOCKET;
static NP_Addr NP_PuncherAddr = {0};
static char NP_ServerHost[128] = {0};

static NP_Data *NP_QueueIn = NULL, *NP_QueueOut = NULL;
static NP_Metadata NP_LobbyMetadataIn = {0}, NP_LobbyMetadataOut = {0},
		   NP_PeerMetadata = {0};

static bool NP_Querying = false;
static NutPunch_Filter NP_Filters[NUTPUNCH_MAX_SEARCH_FILTERS] = {0};
static NutPunch_LobbyInfo NP_Lobbies[NUTPUNCH_MAX_SEARCH_RESULTS] = {0};

static NP_HeartbeatFlagsStorage NP_HeartbeatFlags = 0;
enum {
	NP_HB_Join = 1 << 0,
	NP_HB_Create = 1 << 1,
};

static NP_ResponseFlagsStorage NP_ResponseFlags = 0;
enum {
	NP_R_Master = 1 << 0,
};

static uint16_t* NP_AddrFamily(NP_Addr* addr) {
	return (uint16_t*)&addr->sin_family;
}

static uint32_t* NP_AddrRaw(NP_Addr* addr) {
	return (uint32_t*)&addr->sin_addr.s_addr;
}

static uint16_t* NP_AddrPort(NP_Addr* addr) {
	return &addr->sin_port;
}

static bool NP_AddrEq(NP_Addr a, NP_Addr b) {
	return !NutPunch_Memcmp(&a, &b, 4);
}

static bool NP_AddrNull(NP_Addr addr) {
	static const uint8_t nulladdr[4] = {0};
	if (!*NP_AddrPort(&addr))
		return true;
	return !NutPunch_Memcmp(NP_AddrRaw(&addr), nulladdr, sizeof(nulladdr));
}

static void NP_CleanupPackets(NP_Data** queue) {
	while (*queue) {
		NP_Data* ptr = *queue;
		*queue = ptr->next;
		NutPunch_Free(ptr->data);
		NutPunch_Free(ptr);
	}
}

static void NP_NukeLobbyData() {
	NP_Closing = NP_Querying = false, NP_ResponseFlags = 0;
	NP_LocalPeer = NUTPUNCH_MAX_PLAYERS;
	NP_Memzero(NP_LobbyMetadataIn), NP_Memzero(NP_LobbyMetadataOut);
	NP_Memzero(NP_PeerMetadata), NP_Memzero(NP_Peers);
	NP_Memzero(NP_Filters);
	NP_CleanupPackets(&NP_QueueIn), NP_CleanupPackets(&NP_QueueOut);
}

static void NP_NukeRemote() {
	NP_LobbyId[0] = 0, NP_HeartbeatFlags = 0;
	NP_MemzeroRef(NP_PuncherAddr), NP_Memzero(NP_Peers);
	NP_Memzero(NP_Filters), NP_LastStatus = NPS_Idle;
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
	NP_LastBeating = clock();
	NP_NukeRemote(), NP_NukeLobbyData();
	NP_NukeSocket(&NP_Sock);
}

static void NP_LazyInit() {
	if (NP_InitDone)
		return;
	NP_InitDone = true;

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

static const void*
NP_GetMetadataFrom(const NP_Metadata fields, const char* name, int* size) {
	static char buf[NUTPUNCH_FIELD_DATA_MAX] = {0};
	NP_Memzero(buf);

	int name_size = NP_FieldNameSize(name);
	if (!name_size || !fields)
		goto none;

	for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
		const NutPunch_Field* field = &fields[i];
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

const void* NutPunch_LobbyGet(const char* name, int* size) {
	return NP_GetMetadataFrom(NP_LobbyMetadataIn, name, size);
}

const void* NutPunch_PeerGet(int peer, const char* name, int* size) {
	if (peer < 0 || peer >= NUTPUNCH_MAX_PLAYERS)
		goto null;
	if (!NutPunch_PeerAlive(peer))
		goto null;

	if (peer == NutPunch_LocalPeer())
		return NP_GetMetadataFrom(NP_PeerMetadata, name, size);
	return NP_GetMetadataFrom(NP_Peers[peer].metadata, name, size);

null:
	return NP_GetMetadataFrom(NULL, "", size);
}

static void NP_SetMetadataIn(
	NutPunch_Field* fields, const char* name, int size, const void* data) {
	const int name_size = NP_FieldNameSize(name);
	if (!name_size)
		return;

	if (!data) {
		NP_Warn("No data?");
		return;
	} else if (size < 1) {
		NP_Warn("Invalid metadata field size!");
		return;
	} else if (size > NUTPUNCH_FIELD_DATA_MAX) {
		NP_Warn("Trimming metadata field from %d to %d bytes", size,
			NUTPUNCH_FIELD_DATA_MAX);
		size = NUTPUNCH_FIELD_DATA_MAX;
	}

	static const NutPunch_Field nullfield = {0};
	for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
		NutPunch_Field* field = &fields[i];

		if (!NutPunch_Memcmp(field, &nullfield, sizeof(nullfield)))
			goto set;
		if (NP_FieldNameSize(field->name) == name_size)
			if (!NutPunch_Memcmp(field->name, name, name_size))
				goto set;
		continue;

	set:
		NP_Memzero(field->name);
		NutPunch_Memcpy(field->name, name, name_size);

		NP_Memzero(field->data);
		NutPunch_Memcpy(field->data, data, size);

		field->size = size;
		return;
	}
}

void NutPunch_PeerSet(const char* name, int size, const void* data) {
	return NP_SetMetadataIn(NP_PeerMetadata, name, size, data);
}

void NutPunch_LobbySet(const char* name, int size, const void* data) {
	return NP_SetMetadataIn(NP_LobbyMetadataOut, name, size, data);
}

static bool NP_ResolveNutpuncher() {
	NP_LazyInit();

	struct addrinfo *resolved = NULL, hints = {0};
	hints.ai_family = AF_INET, hints.ai_socktype = SOCK_DGRAM,
	hints.ai_protocol = IPPROTO_UDP;

	if (!NP_ServerHost[0]) {
		NutPunch_SetServerAddr(NUTPUNCH_DEFAULT_SERVER);
		NP_Info("Connecting to the public NutPuncher because no server "
			"was explicitly specified");
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
	NutPunch_Memcpy(
		&NP_PuncherAddr, resolved->ai_addr, resolved->ai_addrlen);
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
	const char* shit = (const char*)&argp;
	return !setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, shit, sizeof(argp));
}

static bool NP_BindSocket() {
	const clock_t range = NUTPUNCH_PORT_MAX - NUTPUNCH_PORT_MIN + 1;
	NP_Addr local = {0};

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
	*NP_AddrPort(&local) = htons(NUTPUNCH_PORT_MIN + clock() % range);
	*NP_AddrRaw(&local) = htonl(INADDR_ANY);

	if (!bind(NP_Sock, (struct sockaddr*)&local, sizeof(local)))
		return true;

	NP_Warn("Failed to bind a UDP socket (%d)", NP_SockError());
sockfail:
	NP_NukeSocket(&NP_Sock);
	return false;
}

static bool NutPunch_Connect(const char* lobby_id, bool sane) {
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
	NP_LastBeating = clock();
	NP_ResolveNutpuncher();

	NP_Info("Ready to send heartbeats");
	NP_LastStatus = NPS_Online;
	NP_Memzero(NP_LastError);

	if (!lobby_id)
		return true;
	NutPunch_SNPrintF(NP_LobbyId, sizeof(NP_LobbyId), "%s", lobby_id);
	return true;
}

bool NutPunch_Host(const char* lobby_id, int players) {
	NP_LazyInit();
	NP_HeartbeatFlags = NP_HB_Join | NP_HB_Create;
	NutPunch_SetMaxPlayers(players);
	return NutPunch_Connect(lobby_id, true);
}

bool NutPunch_Join(const char* lobby_id) {
	NP_LazyInit();
	NP_HeartbeatFlags = NP_HB_Join;
	return NutPunch_Connect(lobby_id, true);
}

void NutPunch_SetMaxPlayers(int players) {
	const int DEFAULT = 4;

	if (players <= 1 || players > NUTPUNCH_MAX_PLAYERS) {
		NP_Warn("Requesting %d players (passed %d)", DEFAULT, players);
		players = DEFAULT;
	}

	NP_HeartbeatFlags &= 0xF;
	NP_HeartbeatFlags |= (players - 1) << 4;
}

int NutPunch_GetMaxPlayers() {
	if (!NutPunch_IsOnline())
		return 0;
	return 1 + ((NP_ResponseFlags & 0xF0) >> 4);
}

void NutPunch_FindLobbies(int filter_count, const NutPunch_Filter* filters) {
	if (filter_count < 1) {
		NP_Warn("No filters given to `NutPunch_FindLobbies`; this is a "
			"no-op");
		return;
	}

	if (filter_count > NUTPUNCH_MAX_SEARCH_FILTERS) {
		NP_Warn("Filter count exceeded in `NutPunch_FindLobbies`; "
			"truncating the input");
		filter_count = NUTPUNCH_MAX_SEARCH_FILTERS;
	}

	NP_LazyInit();
	NP_Querying = NutPunch_Connect(NULL, false);
	NutPunch_Memcpy(NP_Filters, filters, filter_count * sizeof(*filters));
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

// NOTE: formats both the address portion and the port.
static const char* NP_FormatAddr(NP_Addr addr) {
	static char buf[64] = "";
	NP_Memzero(buf);

	const uint16_t p = ntohs(*NP_AddrPort(&addr));
	const char* s = inet_ntoa(addr.sin_addr);
	NutPunch_SNPrintF(buf, sizeof(buf), "%s port %d", s, p);

	return buf;
}

static void NP_KillPeer(int peer) {
	NP_MemzeroRef(NP_Peers[peer]);
}

static void NP_HandleShalom(NP_Message msg) {
	const uint8_t idx = *msg.data++;
	if (idx >= NUTPUNCH_MAX_PLAYERS)
		return;

	NP_Peers[idx].addr = msg.addr;
	NP_Peers[idx].last_beating = clock();
	NP_Trace("SHALOM %d = %s", idx, NP_FormatAddr(msg.addr));

	NP_Metadata* const pm = &NP_Peers[idx].metadata;
	NutPunch_Memcpy(pm, msg.data, sizeof(NP_Metadata));
}

static void NP_HandleGTFO(NP_Message msg) {
	if (!NP_AddrEq(msg.addr, NP_PuncherAddr))
		return;

	// Have to work around designated array initializers for C++ NutPuncher
	// to compile...
	const char* errors[NPE_Max] = {0};
	errors[NPE_NoSuchLobby] = "Lobby doesn't exist";
	errors[NPE_LobbyExists] = "Lobby already exists";
	errors[NPE_LobbyFull] = "Lobby is full";
	errors[NPE_Sybau] = "sybau :wilted_rose:";

	int idx = msg.data[0];
	if (idx <= NPE_Ok || idx >= NPE_Max)
		NP_Warn("Unidentified error");
	else
		NP_Warn("%s", errors[idx]);
	NP_LastStatus = NPS_Error;
}

static void NP_PrintLocalPeer(const uint8_t* data) {
	NP_Addr addr = {0};
	*NP_AddrFamily(&addr) = AF_INET;
	NutPunch_Memcpy(NP_AddrRaw(&addr), data, 4), data += 4;
	*NP_AddrPort(&addr) = *(uint16_t*)data, data += 2;
	NP_Info("Server thinks you are %s", NP_FormatAddr(addr));
}

static int NP_SendDirectly(NP_Addr dest, const void* data, int len) {
	struct sockaddr* shit_dest = (struct sockaddr*)&dest;
	const char* shit_data = (const char*)data;
	return sendto(NP_Sock, shit_data, len, 0, shit_dest, sizeof(dest));
}

static void
NP_SendTimesDirectly(int times, NP_Addr dest, const void* data, int len) {
	while (times-- > 0)
		NP_SendDirectly(dest, data, len);
}

static void NP_SayShalom(int idx, const uint8_t* data) {
	if (NP_Sock == NUTPUNCH_INVALID_SOCKET)
		return;
	if (idx == NP_LocalPeer)
		return;

	NP_Addr addr = {0};
	*NP_AddrFamily(&addr) = AF_INET;
	NutPunch_Memcpy(NP_AddrRaw(&addr), data, 4), data += 4;
	*NP_AddrPort(&addr) = *(uint16_t*)data, data += 2;

	if (NP_AddrNull(addr)) {
		NP_KillPeer(idx); // dead on the NutPuncher's side
		return;
	}

	NP_Peer* peer = &NP_Peers[idx];
	clock_t now = clock(),
		timeout_period = NUTPUNCH_PEER_TIMEOUT_SECS * CLOCKS_PER_SEC;

	const bool overtime = now - peer->first_shalom >= timeout_period,
		   timed_out = peer->first_shalom && overtime;

	if (!NutPunch_PeerAlive(idx) && timed_out) {
		NP_Warn("Failed to establish a connection to peer %d", idx + 1);
		peer->first_shalom = now;
		return;
	}

	if (!peer->first_shalom)
		peer->first_shalom = now;

	static uint8_t shalom[sizeof(NP_Header) + 1 + sizeof(NP_Metadata)]
		= "SHLM";

	uint8_t* ptr = &shalom[sizeof(NP_Header)];
	*ptr = NP_LocalPeer, ptr += 1;
	NutPunch_Memcpy(ptr, NP_PeerMetadata, sizeof(NP_Metadata));

	NP_SendDirectly(addr, shalom, sizeof(shalom));
	NP_Trace("SENT HI %s", NP_FormatAddr(addr));
}

static void NP_HandleBeating(NP_Message msg) {
	NP_Trace("RECEIVED A BEATING FROM %s", NP_FormatAddr(msg.addr));

	if (!NP_AddrEq(msg.addr, NP_PuncherAddr))
		return;
	NP_LastBeating = clock();

	const bool just_joined = NP_LocalPeer == NUTPUNCH_MAX_PLAYERS,
		   was_slave = !NutPunch_IsMaster();
	const ptrdiff_t stride = NUTPUNCH_ADDRESS_SIZE;

	NP_LocalPeer = *msg.data++, NP_ResponseFlags = *msg.data++;
	if (just_joined)
		NP_PrintLocalPeer(msg.data + NP_LocalPeer * stride);
	if (NutPunch_IsMaster() && was_slave)
		NP_Info("We're the lobby's master now");

	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		NP_SayShalom(i, msg.data);
		msg.data += NUTPUNCH_ADDRESS_SIZE;
	}

	NutPunch_Memcpy(NP_LobbyMetadataIn, msg.data, sizeof(NP_Metadata));
}

static void NP_HandleListing(NP_Message msg) {
	NP_Trace("RECEIVED A LISTING FROM %s", NP_FormatAddr(msg.addr));

	if (!NP_AddrEq(msg.addr, NP_PuncherAddr))
		return;

	NP_LastBeating = clock();

	const size_t idlen = sizeof(NutPunch_Id);
	NP_Memzero(NP_Lobbies);

	for (int i = 0; i < NUTPUNCH_MAX_SEARCH_RESULTS; i++) {
		NP_Lobbies[i].players = *(uint8_t*)(msg.data++);
		NP_Lobbies[i].capacity = *(uint8_t*)(msg.data++);

		NutPunch_Memcpy(NP_Lobbies[i].name, msg.data, idlen);
		NP_Lobbies[i].name[idlen] = '\0', msg.data += idlen;
	}
}

static void NP_HandleData(NP_Message msg) {
	int peer_idx = NUTPUNCH_MAX_PLAYERS;

	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		if (NP_AddrEq(msg.addr, NP_Peers[i].addr)) {
			peer_idx = i;
			break;
		}
	}

	if (peer_idx == NUTPUNCH_MAX_PLAYERS)
		return;

	NP_Peer* const peer = &NP_Peers[peer_idx];
	peer->last_beating = clock();

	const NP_PacketIdx index_as_recv = *(NP_PacketIdx*)msg.data,
			   index = ntohl(index_as_recv);
	msg.size -= sizeof(index), msg.data += sizeof(index);

	NP_Data* packet = (NP_Data*)NutPunch_Malloc(sizeof(*packet));
	packet->data = (char*)NutPunch_Malloc(msg.size);
	NutPunch_Memcpy(packet->data, msg.data, msg.size);
	packet->peer = peer_idx, packet->size = msg.size;
	packet->index = index;

	if (!index) {
		packet->next = NP_QueueIn;
		NP_QueueIn = packet;
		return;
	}

	for (NP_Data* ptr = peer->out_of_order; ptr; ptr = ptr->next)
		if (ptr->index == index) // check for duplicates
			return;

	packet->next = peer->out_of_order;
	peer->out_of_order = packet;

	for (;;) {
		NP_Data *ptr = peer->out_of_order, *prev = NULL;
		for (; ptr; prev = ptr, ptr = ptr->next)
			if (ptr->index == peer->recv_counter + 1)
				break;
		if (!ptr)
			break;

		peer->recv_counter++;
		if (prev)
			prev->next = ptr->next;
		else
			peer->out_of_order = ptr->next;
		ptr->next = NP_QueueIn, NP_QueueIn = ptr;
	}

	static char ack[sizeof(NP_Header) + sizeof(index)] = "ACKY";
	char* const ptr = ack + sizeof(NP_Header);
	NutPunch_Memcpy(ptr, &index_as_recv, sizeof(index));
	NP_SendDirectly(peer->addr, ack, sizeof(ack));
}

static void NP_HandleAcky(NP_Message msg) {
	NP_PacketIdx index = ntohl(*(NP_PacketIdx*)msg.data);
	for (NP_Data* ptr = NP_QueueOut; ptr; ptr = ptr->next)
		if (ptr->index == index) {
			ptr->dead = true;
			return;
		}
}

static bool NP_SendHeartbeat() {
	if (NP_Sock == NUTPUNCH_INVALID_SOCKET)
		return true;

	static char heartbeat[sizeof(NP_Header) + sizeof(NP_Heartbeat)] = {0};
	NP_Memzero(heartbeat);

	char* ptr = heartbeat;
	if (NP_Querying) {
		NutPunch_Memcpy(ptr, "LIST", sizeof(NP_Header));
		ptr += sizeof(NP_Header);

		NutPunch_Memcpy(ptr, NP_Filters, sizeof(NP_Filters));
		ptr += sizeof(NP_Filters);
	} else {
		NutPunch_Memcpy(ptr, "JOIN", sizeof(NP_Header));
		ptr += sizeof(NP_Header);

		NutPunch_Memset(ptr, 0, sizeof(NutPunch_Id));
		NutPunch_Memcpy(ptr, NP_LobbyId, sizeof(NutPunch_Id));
		ptr += sizeof(NutPunch_Id);

		// TODO: make sure to correct endianness when multibyte flags
		// become a thing.
		*(NP_HeartbeatFlagsStorage*)ptr = NP_HeartbeatFlags,
		ptr += sizeof(NP_HeartbeatFlags);

		NutPunch_Memcpy(ptr, NP_LobbyMetadataOut, sizeof(NP_Metadata));
		ptr += sizeof(NP_Metadata);
	}

	const int len = (int)(ptr - heartbeat);
	if (0 <= NP_SendDirectly(NP_PuncherAddr, heartbeat, len))
		return true;

	const int err = NP_SockError();
	switch (err) {
	case NP_WouldBlock:
	case NP_ConnReset:
		return true;
	default:
		NP_Warn("Failed to send heartbeat to NutPuncher (%d)", err);
		return false;
	}
}

typedef enum {
	NP_RS_SockFail = -1,
	NP_RS_Again = 0,
	NP_RS_Done = 1,
} NP_ReceiveStatus;

static int NP_UglyRecvfrom(NP_Addr* addr, char* buf, int size) {
	socklen_t addr_size = sizeof(*addr);
	struct sockaddr* shit_addr = (struct sockaddr*)addr;
	return recvfrom(NP_Sock, buf, size, 0, shit_addr, &addr_size);
}

static NP_ReceiveStatus NP_ReceiveShit() {
	if (NP_Sock == NUTPUNCH_INVALID_SOCKET)
		return NP_RS_Done;

	static char buf[NUTPUNCH_BUFFER_SIZE] = {0};
	NP_Addr addr = {0};

	int size = NP_UglyRecvfrom(&addr, buf, sizeof(buf));
	if (size < 0) {
		const int err = NP_SockError();
		if (err == NP_WouldBlock || err == NP_ConnReset)
			return NP_RS_Done;
		if (err == NP_TooFat)
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

static void NP_PruneOutQueue() {
	for (NP_Data *ptr = NP_QueueOut, *prev = NULL; ptr;) {
		if (!ptr->dead) {
			prev = ptr, ptr = ptr->next;
			continue;
		}

		if (prev)
			prev->next = ptr->next;
		else
			NP_QueueOut = ptr->next;

		NP_Data* tmp = ptr;
		ptr = ptr->next;
		NutPunch_Free(tmp->data), NutPunch_Free(tmp);
	}
}

static void NP_FlushOutQueue() {
	for (NP_Data* cur = NP_QueueOut; cur; cur = cur->next) {
		if (!NutPunch_PeerAlive(cur->peer)) {
			cur->dead = true;
			continue;
		}

		// Send & pop normally since a bounce of -1 makes it an
		// unreliable packet.
		if (cur->bounce < 0)
			cur->dead = true;
		// Otherwise, check if it's about to bounce, to resend it.
		else if (cur->bounce > 0)
			if (--cur->bounce > 0)
				continue;
		cur->bounce = NUTPUNCH_BOUNCE_TICKS;

		if (NP_Sock == NUTPUNCH_INVALID_SOCKET) {
			cur->dead = true;
			continue;
		}

		NP_Addr addr = NP_Peers[cur->peer].addr;
		int result = NP_SendDirectly(addr, cur->data, (int)cur->size);
		if (!result)
			NP_KillPeer(cur->peer);
		if (result >= 0 || NP_SockError() == NP_WouldBlock
			|| NP_SockError() == NP_ConnReset)
			continue;
		NP_Warn("Failed to send data to peer #%d (%d)", cur->peer + 1,
			NP_SockError());
		NP_NukeLobbyData();
		return;
	}
}

static void NP_SendGoodbyes() {
	static char bye[4] = {'D', 'I', 'S', 'C'};
	NP_SendTimesDirectly(10, NP_PuncherAddr, bye, sizeof(bye));
}

static void NP_NetworkUpdate() {
	clock_t now = clock(),
		server_timeout = NUTPUNCH_SERVER_TIMEOUT_SECS * CLOCKS_PER_SEC,
		peer_timeout = NUTPUNCH_PEER_TIMEOUT_SECS * CLOCKS_PER_SEC;

	if (now - NP_LastBeating >= server_timeout) {
		NP_Warn("NutPuncher connection timed out!");
		goto error;
	}

	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		const clock_t diff = now - NP_Peers[i].last_beating;
		const bool timed_out = diff >= peer_timeout;
		if (i == NutPunch_LocalPeer())
			continue;
		if (!timed_out || !NutPunch_PeerAlive(i))
			continue;
		NP_Info("Peer %d timed out", i + 1);
		NP_KillPeer(i);
	}

	if (!NP_SendHeartbeat())
		goto sockfail;

	for (;;) {
		if (NP_LastStatus == NPS_Error) // happens after handling a GTFO
			return;
		switch (NP_ReceiveShit()) {
		case NP_RS_SockFail:
			goto sockfail;
		case NP_RS_Done:
			goto flush;
		case NP_RS_Again:
			break;
		}
	}

flush:
	if (NP_Closing)
		NP_SendGoodbyes();

	NP_PruneOutQueue(), NP_FlushOutQueue();
	return;

sockfail:
	NP_Warn("Something went wrong with your socket!");
error:
	NP_LastStatus = NPS_Error;
}

NutPunch_UpdateStatus NutPunch_Update() {
	NP_LazyInit();

	if (NP_LastStatus == NPS_Idle || NP_Sock == NUTPUNCH_INVALID_SOCKET)
		return NPS_Idle;

	NP_LastStatus = NPS_Online, NP_NetworkUpdate();

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

bool NutPunch_HasMessage() {
	return NP_QueueIn != NULL;
}

int NutPunch_NextMessage(void* out, int* size) {
	if (*size < NP_QueueIn->size) {
		NP_Warn("Not enough memory allocated to copy the next packet");
		return NUTPUNCH_MAX_PLAYERS;
	}

	if (size)
		*size = (int)NP_QueueIn->size;
	if (out)
		NutPunch_Memcpy(out, NP_QueueIn->data, NP_QueueIn->size);
	NutPunch_Free(NP_QueueIn->data);

	int source_peer = NP_QueueIn->peer;
	if (source_peer > NUTPUNCH_MAX_PLAYERS)
		source_peer = NUTPUNCH_MAX_PLAYERS;

	NP_Data* next = NP_QueueIn->next;
	NutPunch_Free(NP_QueueIn);

	NP_QueueIn = next;
	return source_peer;
}

static void NP_SendPro(int peer, const void* data, int size, bool reliable) {
	NP_LazyInit();

	if (!data) {
		NP_Warn("No data?");
		return;
	}

	if (!NutPunch_PeerAlive(peer) || NutPunch_LocalPeer() == peer)
		return;

	if (size > NUTPUNCH_BUFFER_SIZE - 32) {
		NP_Warn("Ignoring a huge packet");
		return;
	}

	NP_PacketIdx index = 0;
	if (reliable)
		index = ++NP_Peers[peer].send_counter;
	const NP_PacketIdx net_index = htonl(index);

	static char buf[NUTPUNCH_BUFFER_SIZE] = "DATA";
	char* ptr = buf + sizeof(NP_Header);

	NutPunch_Memcpy(ptr, &net_index, sizeof(net_index));
	ptr += sizeof(net_index);

	NutPunch_Memcpy(ptr, data, size);
	ptr += size;

	NP_Data* next = NP_QueueOut;
	NP_QueueOut = (NP_Data*)NutPunch_Malloc(sizeof(*next));
	NP_QueueOut->next = next, NP_QueueOut->peer = peer;
	NP_QueueOut->size = ptr - buf, NP_QueueOut->bounce = index ? 0 : -1;
	NP_QueueOut->index = index, NP_QueueOut->dead = false;
	NP_QueueOut->data = (char*)NutPunch_Malloc(NP_QueueOut->size);
	NutPunch_Memcpy(NP_QueueOut->data, buf, NP_QueueOut->size);
}

void NutPunch_Send(int peer, const void* data, int size) {
	NP_SendPro(peer, data, size, false);
}

void NutPunch_SendReliably(int peer, const void* data, int size) {
	NP_SendPro(peer, data, size, true);
}

const NutPunch_LobbyInfo* NutPunch_GetLobby(int index) {
	NP_LazyInit();
	if (index >= 0 && index < NutPunch_LobbyCount())
		return &NP_Lobbies[index];
	return NULL;
}

int NutPunch_LobbyCount() {
	NP_LazyInit();
	static const NutPunch_Id nully = {0};
	for (int i = 0; i < NUTPUNCH_MAX_SEARCH_RESULTS; i++)
		if (!NutPunch_Memcmp(NP_Lobbies[i].name, nully, sizeof(nully)))
			return i;
	return NUTPUNCH_MAX_SEARCH_RESULTS;
}

int NutPunch_PeerCount() {
	int count = 0;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		count += NutPunch_PeerAlive(i);
	return count;
}

bool NutPunch_PeerAlive(int peer) {
	if (peer < 0 || peer >= NUTPUNCH_MAX_PLAYERS)
		return false;
	if (!NutPunch_IsReady())
		return false;
	if (NutPunch_LocalPeer() == peer)
		return true;
	return 0 != *NP_AddrPort(&NP_Peers[peer].addr);
}

int NutPunch_LocalPeer() {
	if (!NutPunch_IsOnline())
		return NUTPUNCH_MAX_PLAYERS;
	return NP_LocalPeer;
}

bool NutPunch_IsOnline() {
	return NP_LastStatus == NPS_Online;
}

bool NutPunch_IsReady() {
	return NutPunch_LocalPeer() != NUTPUNCH_MAX_PLAYERS;
}

bool NutPunch_IsMaster() {
	if (!NutPunch_IsReady())
		return false;
	return (NP_ResponseFlags & NP_R_Master) != 0;
}

const char* NutPunch_Basename(const char* path) {
	size_t len = 0; // clang-format off
	for (len = 0; path[len]; len++) {} // clang-format on
	for (size_t i = len - 2; i >= 0; i--)
		if (path[i] == '/' || path[i] == '\\')
			return &path[i + 1];
	return path;
}

#ifdef NUTPUNCH_WINDOSE
#define NP_SleepMs(ms) Sleep(ms)
#else
#include <time.h>
static void NP_SleepMs(int ms) {
	// Stolen from: <https://stackoverflow.com/a/1157217>
	struct timespec ts;
	ts.tv_sec = (ms) / 1000;
	ts.tv_nsec = ((ms) % 1000) * 1000000;
	int res;
	do
		res = nanosleep(&ts, &ts);
	while (res && errno == EINTR);
}
#endif

#endif

#ifdef __cplusplus
}
#endif

#endif
