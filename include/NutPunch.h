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

#pragma once

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
#define NUTPUNCH_SERVER_TIMEOUT_SECS 3
#endif

#ifndef NUTPUNCH_PEER_TIMEOUT_SECS
/// How many seconds to wait for a peer to respond before timing out.
#define NUTPUNCH_PEER_TIMEOUT_SECS 5
#endif

/// The maximum length of a lobby identifier excluding the null terminator. Not customizable.
#define NUTPUNCH_ID_MAX (32)

/// How many bytes to reserve for every network packet.
#define NUTPUNCH_BUFFER_SIZE (8192)

/// The maximum amount of results `NutPunch_LobbyList` can provide.
#define NUTPUNCH_SEARCH_RESULTS_MAX (32)

/// The maximum amount of filters you can pass to `NutPunch_FindLobbies`.
#define NUTPUNCH_SEARCH_FILTERS_MAX (8)

/// Maximum length of a metadata field name.
#define NUTPUNCH_FIELD_NAME_MAX (8)

/// Maximum volume of data you can store in a metadata field.
#define NUTPUNCH_FIELD_DATA_MAX (16)

/// Maximum amount of metadata fields per lobby/player.
#define NUTPUNCH_MAX_FIELDS (12)

/// How many updates to wait before resending a reliable packet.
#define NUTPUNCH_BOUNCE_TICKS (60)

#define NUTPUNCH_PORT_MIN ((uint16_t)(NUTPUNCH_SERVER_PORT + 1))
#define NUTPUNCH_PORT_MAX ((uint16_t)(NUTPUNCH_PORT_MIN + 512))

#define NUTPUNCH_HEADER_SIZE (4)
#define NUTPUNCH_ADDRESS_SIZE (6)

#define NUTPUNCH_RESPONSE_SIZE                                                                                         \
	(NUTPUNCH_HEADER_SIZE + 1 + 1 + NUTPUNCH_MAX_PLAYERS * NUTPUNCH_ADDRESS_SIZE                                   \
		+ (NUTPUNCH_MAX_PLAYERS + 1) * NUTPUNCH_MAX_FIELDS * (int)sizeof(NutPunch_Field))

#define NUTPUNCH_HEARTBEAT_SIZE                                                                                        \
	(NUTPUNCH_HEADER_SIZE + NUTPUNCH_ID_MAX + (int)sizeof(NP_HeartbeatFlagsStorage)                                \
		+ 2 * NUTPUNCH_MAX_FIELDS * (int)sizeof(NutPunch_Field))

#ifndef NUTPUNCH_NOSTD
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#endif

typedef struct {
	char name[NUTPUNCH_FIELD_NAME_MAX];
	char data[NUTPUNCH_FIELD_DATA_MAX];
	uint8_t size;
} NutPunch_Field; // DOT NOT MOVE OUT, used in `NUTPUNCH_*_SIZE` constants above!!!

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
	char name[NUTPUNCH_ID_MAX + 1];
	int players, capacity;
} NutPunch_LobbyInfo;

enum {
	NPS_Error,
	NPS_Idle,
	NPS_Online,
};

enum {
	NPF_Not = 1 << 0,
	NPF_Eq = 1 << 1,
	NPF_Less = 1 << 2,
	NPF_Greater = 1 << 3,
};

enum {
	NPE_Ok,
	NPE_Sybau,
	NPE_NoSuchLobby,
	NPE_LobbyExists,
	NPE_LobbyFull,
	NPE_Max,
};

/// Set a custom NutPuncher server address.
void NutPunch_SetServerAddr(const char* hostname);

/// Join a lobby by its ID. If no lobby exists with this ID, spit an error status out of `NutPunch_Update()`.
int NutPunch_Join(const char* lobby_id);

/// Host a lobby with the specified ID and maximum player count.
///
/// If the lobby already exists, an error status spits out of `NutPunch_Update()` rather than immediately.
int NutPunch_Host(const char* lobby_id, int players);

/// Change the maximum player count after calling `NutPunch_Host`.
void NutPunch_SetMaxPlayers(int players);

/// Get the maximum player count of the lobby you are in. Returns 0 if you aren't in a lobby.
int NutPunch_GetMaxPlayers();

/// Call this at the end of your program to run semi-important cleanup.
void NutPunch_Cleanup();

/// Call this every frame to update nutpunch. Returns one of the `NPS_*` constants.
int NutPunch_Update();

/// Request lobby metadata to be set. Can be called multiple times in a row. Will send out metadata changes on
/// `NutPunch_Update()`, and won't do anything unless you're the lobby's master.
///
/// See `NUTPUNCH_FIELD_NAME_MAX` and `NUTPUNCH_FIELD_DATA_MAX` for limitations on the amount of data you can squeeze.
void NutPunch_LobbySet(const char* name, int size, const void* data);

/// Request lobby metadata. Sets `size` to the field's actual size if the pointer isn't `NULL`.
///
/// The resulting pointer is actually a static allocation, so don't rely too much on it; its data will change after the
/// next `NutPunch_LobbyGet` call.
///
/// Note: you cannot query a lobby's metadata if you aren't connected to it. That's what `NutPunch_FindLobbies` uses
/// filters for.
void* NutPunch_LobbyGet(const char* name, int* size);

/// Request your peer-specific metadata to be set. Works the same way as `NutPunch_LobbySet` otherwise.
void NutPunch_PeerSet(const char* name, int size, const void* data);

/// Request metadata for a specific peer. Works the same way as `NutPunch_LobbyGet` otherwise.
void* NutPunch_PeerGet(int peer, const char* name, int* size);

/// Check if there is a packet waiting in the receiving queue. Retrieve it with `NutPunch_NextMessage()`.
bool NutPunch_HasMessage();

/// Retrieve the next packet in the receiving queue. Returns the index of the peer who sent it.
///
/// In case of an error, logs it and returns `NUTPUNCH_MAX_PLAYERS`.
///
/// Size must be set to the output buffer's size. Passing an output buffer that is too small to contain the message data
/// is considered an error.
int NutPunch_NextMessage(void* out, int* size);

/// Send data to specified peer. For reliable packet delivery, use `NutPunch_SendReliably`.
void NutPunch_Send(int peer, const void* data, int size);

/// Send data to specified peer expecting them to acknowledge the fact of reception.
void NutPunch_SendReliably(int peer, const void* data, int size);

/// Count how many "live" peers we have a route to, including our local peer.
///
/// Do not use this as an upper bound for iterating over peers; they can come in any order and with gaps in-between. For
/// that, see `NutPunch_PeerAlive`.
int NutPunch_PeerCount();

/// Return true if you are connected to the peer with the specified index.
///
/// If you're iterating over peers, use `NUTPUNCH_MAX_PLAYERS` as the upper index bound, and check their status using
/// this function.
bool NutPunch_PeerAlive(int peer);

/// Get the local peer's index. Returns `NUTPUNCH_MAX_PLAYERS` if this fails for any reason.
int NutPunch_LocalPeer();

/// Check if we're the lobby's master.
bool NutPunch_IsMaster();

/// Call this to gracefully disconnect from the lobby.
void NutPunch_Disconnect();

/// Query the lobbies list given a set of filters. Use `NutPunch_GetLobby` to retrieve the results.
///
/// The list of lobbies will update after a few ticks of `NutPunch_Update`. Don't expect immediate results.
///
/// Each filter consists of either a special or a named metadata field, with a corresponding value to compare it to. All
/// filters must match in order for a lobby to be listed.
///
/// Bitwise-or the `NPF_*` constants to set a filter's comparison flags.
///
/// To query "special" fields such as lobby capacity, use one of the `NPSF_*` constants with an `int8_t` value to
/// compare it against. For example, to query lobbies for exactly a 2-player duo to play:
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
/// To query metadata fields, `memcpy` their names and values into the filter's `field` property. The comparison will be
/// performed bytewise in a fashion similar to `memcmp`.
void NutPunch_FindLobbies(int filter_count, const NutPunch_Filter* filters);

/// Extract lobby info after `NutPunch_FindLobbies`. Updates every call to `NutPunch_Update`.
const NutPunch_LobbyInfo* NutPunch_GetLobby(int index);

/// Count how many lobbies were found after `NutPunch_FindLobbies`. Updates every call to `NutPunch_Update`.
int NutPunch_LobbyCount();

/// Use this to reset the underlying socket in case of an inexplicable error.
void NutPunch_Reset();

/// Get the human-readable description of the latest error in `NutPunch_Update()`.
const char* NutPunch_GetLastError();

/// Get a file's basename (the name without directory). Used internally in `NP_Log`.
const char* NutPunch_Basename(const char* path);

#ifndef NutPunch_Log
#define NutPunch_Log(msg, ...)                                                                                         \
	do {                                                                                                           \
		fprintf(stdout, "(%s:%d) " msg "\n", NutPunch_Basename(__FILE__), __LINE__, ##__VA_ARGS__);            \
		fflush(stdout);                                                                                        \
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
#define NP_MessageSize WSAEMSGSIZE

#else // everything non-winsoque comes from: <https://stackoverflow.com/a/28031039>

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
#define NP_MessageSize EMSGSIZE

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
#define NP_Memzero2(ptr) NutPunch_Memset(ptr, 0, sizeof(*(ptr)))

#define NP_Info(...) NutPunch_Log("INFO: " __VA_ARGS__)
#define NP_Warn(...)                                                                                                   \
	do {                                                                                                           \
		NutPunch_Log("WARN: " __VA_ARGS__);                                                                    \
		NutPunch_SNPrintF(NP_LastError, sizeof(NP_LastError), __VA_ARGS__);                                    \
	} while (0)

#ifdef NUTPUNCH_TRACING
#define NP_Trace(...) NutPunch_Log("TRACE: " __VA_ARGS__)
#else
#define NP_Trace(...)                                                                                                  \
	do {                                                                                                           \
	} while (0)
#endif

typedef uint32_t NP_PacketIdx;

typedef struct {
	struct sockaddr_storage raw;
} NP_Addr;

typedef struct {
	NP_Addr addr;
	clock_t last_beating, first_shalom;
} NP_Peer;

typedef struct NP_DataMessage {
	char* data;
	struct NP_DataMessage* next;
	NP_PacketIdx index;
	uint32_t size;
	uint8_t peer, dead;
	int16_t bounce;
} NP_DataMessage;

typedef struct {
	NP_Addr addr;
	int size;
	const uint8_t* data;
} NP_Message;

typedef struct {
	const char identifier[NUTPUNCH_HEADER_SIZE];
	void (*const handle)(NP_Message);
	const int packet_size;
} NP_MessageType;

#define NP_BEAT_LEN (NUTPUNCH_RESPONSE_SIZE - NUTPUNCH_HEADER_SIZE)
#define NP_LIST_LEN (NUTPUNCH_SEARCH_RESULTS_MAX * (2 + NUTPUNCH_ID_MAX))
#define NP_ACKY_LEN (sizeof(NP_PacketIdx))

static void NP_HandleShalom(NP_Message), NP_HandleDisconnect(NP_Message), NP_HandleGTFO(NP_Message),
	NP_HandleBeating(NP_Message), NP_HandleList(NP_Message), NP_HandleAcky(NP_Message), NP_HandleData(NP_Message);

static const NP_MessageType NP_Messages[] = {
	{{'S', 'H', 'L', 'M'}, NP_HandleShalom,     1          },
	{{'D', 'I', 'S', 'C'}, NP_HandleDisconnect, 0          },
	{{'G', 'T', 'F', 'O'}, NP_HandleGTFO,       1          },
	{{'B', 'E', 'A', 'T'}, NP_HandleBeating,    NP_BEAT_LEN},
	{{'L', 'I', 'S', 'T'}, NP_HandleList,       NP_LIST_LEN},
	{{'A', 'C', 'K', 'Y'}, NP_HandleAcky,       NP_ACKY_LEN},
	{{'D', 'A', 'T', 'A'}, NP_HandleData,       -1         },
};

static char NP_LastError[512] = "";
static clock_t NP_LastBeating = 0;

static bool NP_InitDone = false, NP_Closing = false;
static int NP_LastStatus = NPS_Idle;

static char NP_LobbyId[NUTPUNCH_ID_MAX + 1] = {0};
static NP_Peer NP_Peers[NUTPUNCH_MAX_PLAYERS] = {0};
static uint8_t NP_LocalPeer = NUTPUNCH_MAX_PLAYERS;

static NP_Socket NP_Sock = NUTPUNCH_INVALID_SOCKET;
static NP_Addr NP_PuncherAddr = {0};
static char NP_ServerHost[128] = {0};

static NP_DataMessage *NP_QueueIn = NULL, *NP_QueueOut = NULL;
static NutPunch_Field NP_LobbyMetadataIn[NUTPUNCH_MAX_FIELDS] = {0};
static NutPunch_Field NP_PeerMetadataIn[NUTPUNCH_MAX_PLAYERS][NUTPUNCH_MAX_FIELDS] = {0};
static NutPunch_Field NP_LobbyMetadataOut[NUTPUNCH_MAX_FIELDS] = {0}, NP_PeerMetadataOut[NUTPUNCH_MAX_FIELDS] = {0};

static bool NP_Querying = false;
static NutPunch_Filter NP_Filters[NUTPUNCH_SEARCH_FILTERS_MAX] = {0};
static NutPunch_LobbyInfo NP_Lobbies[NUTPUNCH_SEARCH_RESULTS_MAX] = {0};

typedef uint8_t NP_HeartbeatFlagsStorage;
static NP_HeartbeatFlagsStorage NP_HeartbeatFlags = 0;
enum {
	NP_HB_Join = 1 << 0,
	NP_HB_Create = 1 << 1,
};

typedef uint8_t NP_ResponseFlagsStorage;
static NP_ResponseFlagsStorage NP_ResponseFlags = 0;
enum {
	NP_R_Master = 1 << 0,
};

static uint16_t* NP_AddrFamily(NP_Addr* addr) {
	return (uint16_t*)&((struct sockaddr_in*)&addr->raw)->sin_family;
}

static uint32_t* NP_AddrRaw(NP_Addr* addr) {
	return (uint32_t*)&((struct sockaddr_in*)&addr->raw)->sin_addr.s_addr;
}

static uint16_t* NP_AddrPort(NP_Addr* addr) {
	return &((struct sockaddr_in*)&addr->raw)->sin_port;
}

static bool NP_AddrEq(NP_Addr a, NP_Addr b) {
	return !NutPunch_Memcmp(&a.raw, &b.raw, 4);
}

static bool NP_AddrNull(NP_Addr addr) {
	static const uint8_t nulladdr[4] = {0};
	return !NutPunch_Memcmp(NP_AddrRaw(&addr), nulladdr, sizeof(nulladdr));
}

static void NP_CleanupPackets(NP_DataMessage** queue) {
	while (*queue) {
		NP_DataMessage* ptr = *queue;
		*queue = ptr->next;
		NutPunch_Free(ptr->data);
		NutPunch_Free(ptr);
	}
}

static void NP_NukeLobbyData() {
	NP_Closing = NP_Querying = false, NP_ResponseFlags = 0;
	NP_LocalPeer = NUTPUNCH_MAX_PLAYERS;
	NP_Memzero(NP_LobbyMetadataIn), NP_Memzero(NP_PeerMetadataIn);
	NP_Memzero(NP_LobbyMetadataOut), NP_Memzero(NP_PeerMetadataOut);
	NP_Memzero(NP_Peers), NP_Memzero(NP_Filters);
	NP_CleanupPackets(&NP_QueueIn), NP_CleanupPackets(&NP_QueueOut);
}

static void NP_NukeRemote() {
	NP_LobbyId[0] = 0, NP_HeartbeatFlags = 0;
	NP_Memzero2(&NP_PuncherAddr), NP_Memzero(NP_Peers);
	NP_Memzero(NP_PeerMetadataIn), NP_Memzero(NP_Filters);
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
	NP_LastBeating = clock();
	NP_NukeRemote(), NP_NukeLobbyData();
	NP_NukeSocket(&NP_Sock);
}

static void NP_LazyInit() {
	if (NP_InitDone)
		return;
	NP_InitDone = true;

	NP_Trace("IMPORTANT CONSTANTS:");
	NP_Trace("  RESP = %d, BEAT = %d, LIST = %d", NUTPUNCH_RESPONSE_SIZE, NP_BEAT_LEN, NP_LIST_LEN);

#ifdef NUTPUNCH_WINDOSE
	WSADATA bitch = {0};
	WSAStartup(MAKEWORD(2, 2), &bitch);
#endif

	NP_ResetImpl();

	NutPunch_Log(".-------------------------------------------------------------.");
	NutPunch_Log("| For troubleshooting multiplayer connectivity, please visit: |");
	NutPunch_Log("|    https://github.com/Schwungus/nutpunch#troubleshooting    |");
	NutPunch_Log("'-------------------------------------------------------------'");
}

void NutPunch_Reset() {
	NP_LazyInit();
	NP_ResetImpl();
}

void NutPunch_SetServerAddr(const char* hostname) {
	if (hostname)
		NutPunch_SNPrintF(NP_ServerHost, sizeof(NP_ServerHost), "%s", hostname);
	else
		NP_ServerHost[0] = 0;
}

static int NP_FieldNameSize(const char* name) {
	if (!name)
		return 0;
	for (int i = 0; i < NUTPUNCH_FIELD_NAME_MAX; i++)
		if (!name[i])
			return i;
	return NUTPUNCH_FIELD_NAME_MAX;
}

static void* NP_GetMetadataFrom(const NutPunch_Field* fields, const char* name, int* size) {
	static char buf[NUTPUNCH_FIELD_DATA_MAX] = {0};
	NP_Memzero(buf);

	int name_size = NP_FieldNameSize(name);
	if (!name_size)
		goto none;

	for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
		const NutPunch_Field* ptr = &fields[i];
		if (name_size != NP_FieldNameSize(ptr->name) || NutPunch_Memcmp(ptr->name, name, name_size))
			continue;
		NutPunch_Memcpy(buf, ptr->data, ptr->size);
		if (size)
			*size = ptr->size;
		return buf;
	}
none:
	if (size)
		*size = 0;
	return NULL;
}

void* NutPunch_LobbyGet(const char* name, int* size) {
	return NP_GetMetadataFrom(NP_LobbyMetadataIn, name, size);
}

void* NutPunch_PeerGet(int peer, const char* name, int* size) {
	if (!NutPunch_PeerAlive(peer))
		return NP_GetMetadataFrom(NULL, "", size);
	return NP_GetMetadataFrom(NP_PeerMetadataIn[peer], name, size);
}

static void NP_SetMetadataIn(NutPunch_Field* fields, const char* name, int data_size, const void* data) {
	int name_size = NP_FieldNameSize(name);
	if (!name_size)
		return;

	if (!data) {
		NP_Warn("No data?");
		return;
	} else if (data_size < 1) {
		NP_Warn("Invalid metadata field size!");
		return;
	} else if (data_size > NUTPUNCH_FIELD_DATA_MAX) {
		NP_Warn("Trimming metadata field from %d to %d bytes", data_size, NUTPUNCH_FIELD_DATA_MAX);
		data_size = NUTPUNCH_FIELD_DATA_MAX;
	}

	static const NutPunch_Field nullfield = {0};
	for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
		NutPunch_Field* field = &fields[i];

		if (!NutPunch_Memcmp(field, &nullfield, sizeof(nullfield)))
			goto set;
		if (NP_FieldNameSize(field->name) == name_size && !NutPunch_Memcmp(field->name, name, name_size))
			goto set;
		continue;

	set:
		NP_Memzero(field->name), NutPunch_Memcpy(field->name, name, name_size);
		NP_Memzero(field->data), NutPunch_Memcpy(field->data, data, data_size);
		field->size = data_size;
		return;
	}
}

void NutPunch_PeerSet(const char* name, int size, const void* data) {
	NP_SetMetadataIn(NP_PeerMetadataOut, name, size, data);
}

void NutPunch_LobbySet(const char* name, int size, const void* data) {
	NP_SetMetadataIn(NP_LobbyMetadataOut, name, size, data);
}

static int NP_ResolveNutpuncher() {
	NP_LazyInit();

	struct addrinfo *resolved = NULL, hints = {0};
	hints.ai_family = AF_INET, hints.ai_socktype = SOCK_DGRAM, hints.ai_protocol = IPPROTO_UDP;

	if (!NP_ServerHost[0]) {
		NutPunch_SetServerAddr(NUTPUNCH_DEFAULT_SERVER);
		NP_Info("Connecting to public NutPuncher because no server was specified");
	}

	static char portfmt[8] = {0};
	NutPunch_SNPrintF(portfmt, sizeof(portfmt), "%d", NUTPUNCH_SERVER_PORT);

	if (getaddrinfo(NP_ServerHost, portfmt, &hints, &resolved)) {
		NP_Warn("NutPuncher server address failed to resolve");
		return 0;
	}
	if (!resolved) {
		NP_Warn("Couldn't resolve NutPuncher address");
		return 0;
	}

	NP_Memzero2(&NP_PuncherAddr.raw);
	NutPunch_Memcpy(&NP_PuncherAddr.raw, resolved->ai_addr, resolved->ai_addrlen);
	freeaddrinfo(resolved);

	NP_Info("Resolved NutPuncher address");
	return 1;
}

static int NP_MakeNonblocking(NP_Socket sock) {
#ifdef NUTPUNCH_WINDOSE
	u_long argp = 1;
	return !ioctlsocket(sock, FIONBIO, &argp);
#else
	return !fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
#endif
}

static int NP_MakeReuseAddr(NP_Socket sock) {
#ifdef NUTPUNCH_WINDOSE
	const u_long argp = 1;
#else
	const uint32_t argp = 1;
#endif
	return !setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&argp, sizeof(argp));
}

static int NP_BindSocket() {
	const clock_t range = NUTPUNCH_PORT_MAX - NUTPUNCH_PORT_MIN + 1;
	NP_Addr local = {0};

	NP_LazyInit();
	NP_NukeSocket(&NP_Sock), NP_Sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (NP_Sock == NUTPUNCH_INVALID_SOCKET) {
		NP_Warn("Failed to create the underlying UDP socket (%d)", NP_SockError());
		goto sockfail;
	}

	if (!NP_MakeReuseAddr(NP_Sock)) {
		NP_Warn("Failed to set socket reuseaddr option (%d)", NP_SockError());
		goto sockfail;
	}

	if (!NP_MakeNonblocking(NP_Sock)) {
		NP_Warn("Failed to set socket to non-blocking mode (%d)", NP_SockError());
		goto sockfail;
	}

	*NP_AddrFamily(&local) = AF_INET;
	*NP_AddrPort(&local) = htons(NUTPUNCH_PORT_MIN + clock() % range);
	*NP_AddrRaw(&local) = htonl(INADDR_ANY);

	if (!bind(NP_Sock, (struct sockaddr*)&local.raw, sizeof(local.raw)))
		return 1;

	NP_Warn("Failed to bind a UDP socket (%d)", NP_SockError());
sockfail:
	NP_NukeSocket(&NP_Sock);
	return 0;
}

static int NutPunch_Connect(const char* lobby_id, bool sane) {
	NP_LazyInit();
	NP_NukeLobbyData();

	if (sane && (!lobby_id || !lobby_id[0])) {
		NP_Warn("Lobby ID cannot be null or empty!");
		NP_LastStatus = NPS_Error;
		return 0;
	}

	NP_Memzero2(&NP_PuncherAddr);
	if (!NP_BindSocket()) {
		NutPunch_Reset(), NP_LastStatus = NPS_Error;
		return 0;
	}
	NP_ResolveNutpuncher();

	NP_Info("Ready to send heartbeats");
	NP_LastStatus = NPS_Online;
	NP_Memzero(NP_LastError);

	if (lobby_id)
		NutPunch_SNPrintF(NP_LobbyId, sizeof(NP_LobbyId), "%s", lobby_id);

	return 1;
}

int NutPunch_Host(const char* lobby_id, int players) {
	NP_LazyInit();
	NP_HeartbeatFlags = NP_HB_Join | NP_HB_Create;
	NutPunch_SetMaxPlayers(players);
	return NutPunch_Connect(lobby_id, true);
}

int NutPunch_Join(const char* lobby_id) {
	NP_LazyInit();
	NP_HeartbeatFlags = NP_HB_Join;
	return NutPunch_Connect(lobby_id, true);
}

void NutPunch_SetMaxPlayers(int players) {
	const int DEFAULT = 4;
	if (players < 2 || players >= NUTPUNCH_MAX_PLAYERS) {
		NP_Warn("Requested an invalid player count %d; defaulting to %d", players, DEFAULT);
		players = DEFAULT;
	}
	NP_HeartbeatFlags &= 0xF, NP_HeartbeatFlags |= players << 4;
}

int NutPunch_GetMaxPlayers() {
	if (NP_LastStatus != NPS_Online)
		return 0;
	return (NP_ResponseFlags & 0xF0) >> 4;
}

void NutPunch_FindLobbies(int filter_count, const NutPunch_Filter* filters) {
	if (filter_count < 1) {
		NP_Warn("No filters given to `NutPunch_FindLobbies`; this is a no-op");
		return;
	} else if (filter_count > NUTPUNCH_SEARCH_FILTERS_MAX) {
		NP_Warn("Filter count exceeded in `NutPunch_FindLobbies`; truncating...");
		filter_count = NUTPUNCH_SEARCH_FILTERS_MAX;
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

#ifdef NUTPUNCH_WINDOSE
	// Can't use inet_ntop directly since we're keeping compatibility with WinXP, and that doesn't have it.
	DWORD size = (DWORD)sizeof(buf);
	WSAAddressToString((struct sockaddr*)&addr.raw, sizeof(struct sockaddr_storage), NULL, buf, &size);
#else
	static char inet[64] = "";
	NP_Memzero(inet), inet_ntop(AF_INET, &((struct sockaddr_in*)&addr)->sin_addr, inet, sizeof(inet));
	NutPunch_SNPrintF(buf, sizeof(buf), "%s port %d", inet, ntohs(*NP_AddrPort(&addr)));
#endif

	return buf;
}

static void NP_KillPeer(int peer) {
	NutPunch_Memset(NP_Peers + peer, 0, sizeof(*NP_Peers));
}

static void NP_QueueSend(int peer, const void* data, int size, NP_PacketIdx index) {
	if (!NutPunch_PeerAlive(peer) || NutPunch_LocalPeer() == peer)
		return;
	if (size > NUTPUNCH_BUFFER_SIZE) {
		NP_Warn("Ignoring a huge packet");
		return;
	}

	NP_DataMessage* next = NP_QueueOut;
	NP_QueueOut = (NP_DataMessage*)NutPunch_Malloc(sizeof(*next));
	NP_QueueOut->next = next, NP_QueueOut->peer = peer;
	NP_QueueOut->size = size, NP_QueueOut->bounce = index ? 0 : -1;
	NP_QueueOut->index = index, NP_QueueOut->dead = false;
	NP_QueueOut->data = (char*)NutPunch_Malloc(size);
	NutPunch_Memcpy(NP_QueueOut->data, data, size);
}

static void NP_HandleShalom(NP_Message msg) {
	const uint8_t idx = *msg.data;
	if (idx < NUTPUNCH_MAX_PLAYERS) {
		NP_Peers[idx].addr = msg.addr;
		NP_Peers[idx].last_beating = clock();
	}
	NP_Trace("SHALOM %d = %s", idx, NP_FormatAddr(msg.addr));
}

static void NP_HandleDisconnect(NP_Message msg) {
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		if (NP_AddrEq(msg.addr, NP_Peers[i].addr))
			NP_KillPeer(i);
}

static void NP_HandleGTFO(NP_Message msg) {
	if (!NP_AddrEq(msg.addr, NP_PuncherAddr))
		return;

	// Have to work around designated array initializers for NutPuncher to compile...
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

static void NP_SayShalom(int idx, const uint8_t* data) {
	if (NP_Sock == NUTPUNCH_INVALID_SOCKET)
		return;

	if (NutPunch_PeerAlive(idx) || idx == NP_LocalPeer)
		return;

	NP_Addr peer_addr = {0};
	*NP_AddrFamily(&peer_addr) = AF_INET;
	NutPunch_Memcpy(NP_AddrRaw(&peer_addr), data, 4), data += 4;

	uint16_t* port = NP_AddrPort(&peer_addr);
	*port = *(uint16_t*)data, data += 2;

	if (NP_AddrNull(peer_addr) || !*port)
		return;

	const clock_t now = clock(), peer_timeout = NUTPUNCH_PEER_TIMEOUT_SECS * CLOCKS_PER_SEC;
	if (!NutPunch_PeerAlive(idx) && NP_Peers[idx].first_shalom && now - NP_Peers[idx].first_shalom >= peer_timeout)
	{
		NP_Warn("Cannot establish a connection to peer %d", idx + 1);
		NP_LastStatus = NPS_Error;
		return;
	}
	if (!NP_Peers[idx].first_shalom)
		NP_Peers[idx].first_shalom = now;

	static uint8_t shalom[NUTPUNCH_HEADER_SIZE + 1] = "SHLM";
	shalom[NUTPUNCH_HEADER_SIZE] = NP_LocalPeer;

	int result = sendto(
		NP_Sock, (char*)shalom, sizeof(shalom), 0, (struct sockaddr*)&peer_addr.raw, sizeof(peer_addr.raw));
	NP_Trace("SENT HI %s (%d)", NP_FormatAddr(peer_addr), result >= 0 ? 0 : NP_SockError());
}

static void NP_HandleBeating(NP_Message msg) {
	NP_Trace("RECEIVED A BEATING FROM %s", NP_FormatAddr(msg.addr));

	if (!NP_AddrEq(msg.addr, NP_PuncherAddr))
		return;

	NP_Trace("AND EVEN PROCESSED IT!");
	NP_LastBeating = clock();

	const bool just_joined = NP_LocalPeer == NUTPUNCH_MAX_PLAYERS, was_slave = !NutPunch_IsMaster();
	const int meta_size = NUTPUNCH_MAX_FIELDS * sizeof(NutPunch_Field);
	const ptrdiff_t stride = NUTPUNCH_ADDRESS_SIZE + meta_size;

	NP_LocalPeer = *msg.data++, NP_ResponseFlags = *msg.data++;
	if (just_joined)
		NP_PrintLocalPeer(msg.data + NP_LocalPeer * stride);
	if (NutPunch_IsMaster() && was_slave)
		NP_Info("We're the lobby's master now");

	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		NP_SayShalom(i, msg.data), msg.data += NUTPUNCH_ADDRESS_SIZE;
		NutPunch_Memcpy(NP_PeerMetadataIn[i], msg.data, meta_size), msg.data += meta_size;
	}
	NutPunch_Memcpy(NP_LobbyMetadataIn, msg.data, meta_size);
}

static void NP_HandleList(NP_Message msg) {
	NP_Trace("RECEIVED A LISTING FROM %s", NP_FormatAddr(msg.addr));

	if (!NP_AddrEq(msg.addr, NP_PuncherAddr))
		return;

	NP_Trace("AND EVEN PROCESSED IT!");

	const size_t idlen = NUTPUNCH_ID_MAX;
	NP_Memzero(NP_Lobbies);

	for (int i = 0; i < NUTPUNCH_SEARCH_RESULTS_MAX; i++) {
		NP_Lobbies[i].players = *(uint8_t*)(msg.data++);
		NP_Lobbies[i].capacity = *(uint8_t*)(msg.data++);
		NutPunch_Memcpy(NP_Lobbies[i].name, msg.data, idlen), msg.data += idlen;
		NP_Lobbies[i].name[NUTPUNCH_ID_MAX] = '\0';
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

	NP_Peers[peer_idx].last_beating = clock();

	const NP_PacketIdx net_index = *(NP_PacketIdx*)msg.data, index = ntohl(net_index);
	msg.size -= sizeof(index), msg.data += sizeof(index);

	if (index) {
		static char ack[NUTPUNCH_HEADER_SIZE + sizeof(net_index)] = "ACKY";
		NutPunch_Memcpy(ack + NUTPUNCH_HEADER_SIZE, &net_index, sizeof(net_index));
		NP_QueueSend(peer_idx, ack, sizeof(ack), 0);
	}

	NP_DataMessage* next = NP_QueueIn;
	NP_QueueIn = (NP_DataMessage*)NutPunch_Malloc(sizeof(*next));
	NP_QueueIn->data = (char*)NutPunch_Malloc(msg.size);
	NutPunch_Memcpy(NP_QueueIn->data, msg.data, msg.size);
	NP_QueueIn->peer = peer_idx, NP_QueueIn->size = msg.size;
	NP_QueueIn->next = next;
}

static void NP_HandleAcky(NP_Message msg) {
	NP_PacketIdx index = ntohl(*(NP_PacketIdx*)msg.data);
	for (NP_DataMessage* ptr = NP_QueueOut; ptr; ptr = ptr->next)
		if (ptr->index == index) {
			ptr->dead = true;
			return;
		}
}

static int NP_SendHeartbeat() {
	if (NP_Sock == NUTPUNCH_INVALID_SOCKET)
		return 1;

	static char heartbeat[NUTPUNCH_HEARTBEAT_SIZE] = {0};
	NP_Memzero(heartbeat);

	char* ptr = heartbeat;
	if (NP_Querying) {
		NutPunch_Memcpy(ptr, "LIST", NUTPUNCH_HEADER_SIZE), ptr += NUTPUNCH_HEADER_SIZE;
		NutPunch_Memcpy(ptr, NP_Filters, sizeof(NP_Filters)), ptr += sizeof(NP_Filters);
	} else {
		NutPunch_Memcpy(ptr, "JOIN", NUTPUNCH_HEADER_SIZE), ptr += NUTPUNCH_HEADER_SIZE;

		NutPunch_Memset(ptr, 0, NUTPUNCH_ID_MAX);
		NutPunch_Memcpy(ptr, NP_LobbyId, NUTPUNCH_ID_MAX), ptr += NUTPUNCH_ID_MAX;

		// TODO: make sure to correct endianness when multibyte flags become a thing.
		*(NP_HeartbeatFlagsStorage*)ptr = NP_HeartbeatFlags, ptr += sizeof(NP_HeartbeatFlags);

		const int meta_size = NUTPUNCH_MAX_FIELDS * sizeof(NutPunch_Field);
		NutPunch_Memcpy(ptr, NP_PeerMetadataOut, meta_size), ptr += meta_size;
		NutPunch_Memcpy(ptr, NP_LobbyMetadataOut, meta_size), ptr += meta_size;
	}

	if (0 <= sendto(NP_Sock, heartbeat, (int)(ptr - heartbeat), 0, (const struct sockaddr*)&NP_PuncherAddr.raw,
		    sizeof(NP_PuncherAddr.raw)))
		return 1;

	switch (NP_SockError()) {
	case NP_WouldBlock:
	case NP_ConnReset:
		return 1;
	default:
		NP_Warn("Failed to send heartbeat to NutPuncher (%d)", NP_SockError());
	}

	return 0;
}

static int NP_ReceiveShit() {
	if (NP_Sock == NUTPUNCH_INVALID_SOCKET)
		return 1;

	struct sockaddr_storage addr = {0};
	socklen_t addr_size = sizeof(addr);

	static char buf[NUTPUNCH_BUFFER_SIZE] = {0};
	int size = recvfrom(NP_Sock, buf, sizeof(buf), 0, (struct sockaddr*)&addr, &addr_size);
	if (size < 0) {
		if (NP_SockError() == NP_WouldBlock || NP_SockError() == NP_ConnReset)
			return 1;
		NP_Warn("Failed to receive from NutPuncher (%d)", NP_SockError());
		return -1;
	}
	if (!size) // graceful disconnection
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			if (!NutPunch_Memcmp(&addr, &NP_Peers[i].addr.raw, sizeof(addr))) {
				NP_KillPeer(i);
				return 0;
			}
	if (size < NUTPUNCH_HEADER_SIZE)
		return 0;

	size -= NUTPUNCH_HEADER_SIZE;
	NP_Trace("RECEIVED %d BYTES OF SHIT", size);

	const NP_Addr peer = {.raw = addr};
	for (int i = 0; i < sizeof(NP_Messages) / sizeof(*NP_Messages); i++) {
		const NP_MessageType type = NP_Messages[i];
		if (!NutPunch_Memcmp(buf, type.identifier, NUTPUNCH_HEADER_SIZE)
			&& (type.packet_size < 0 || size == type.packet_size))
		{
			NP_Message msg = {0};
			msg.addr = peer, msg.size = size;
			msg.data = (uint8_t*)(buf + NUTPUNCH_HEADER_SIZE);
			type.handle(msg);
			break;
		}
	}

	return 0;
}

static void NP_PruneOutQueue() {
find_next:
	for (NP_DataMessage* ptr = NP_QueueOut; ptr; ptr = ptr->next) {
		if (!ptr->dead)
			continue;

		for (NP_DataMessage* other = NP_QueueOut; other; other = other->next)
			if (other->next == ptr) {
				other->next = ptr->next;
				NutPunch_Free(ptr->data);
				NutPunch_Free(ptr);
				goto find_next;
			}

		if (ptr == NP_QueueOut)
			NP_QueueOut = ptr->next;
		else
			NP_QueueOut->next = ptr->next;
		NutPunch_Free(ptr->data), NutPunch_Free(ptr);
		goto find_next;
	}
}

static void NP_FlushOutQueue() {
	for (NP_DataMessage* cur = NP_QueueOut; cur; cur = cur->next) {
		if (!NutPunch_PeerAlive(cur->peer)) {
			cur->dead = true;
			continue;
		}

		// Send & pop normally since a bounce of -1 makes it an unreliable packet.
		if (cur->bounce < 0)
			cur->dead = true;
		// Otherwise, check if it's about to bounce, in order to resend it.
		else if (cur->bounce > 0)
			if (--cur->bounce > 0)
				continue;
		cur->bounce = NUTPUNCH_BOUNCE_TICKS;

		if (NP_Sock == NUTPUNCH_INVALID_SOCKET) {
			cur->dead = true;
			continue;
		}

		NP_Addr addr = NP_Peers[cur->peer].addr;
		int result
			= sendto(NP_Sock, cur->data, (int)cur->size, 0, (struct sockaddr*)&addr.raw, sizeof(addr.raw));

		if (!result)
			NP_KillPeer(cur->peer);
		if (result >= 0 || NP_SockError() == NP_WouldBlock || NP_SockError() == NP_ConnReset)
			continue;
		NP_Warn("Failed to send data to peer #%d (%d)", cur->peer + 1, NP_SockError());
		NP_NukeLobbyData();
		return;
	}
}

static void NP_NetworkUpdate() {
	const clock_t now = clock(), server_timeout = NUTPUNCH_SERVER_TIMEOUT_SECS * CLOCKS_PER_SEC,
		      peer_timeout = NUTPUNCH_PEER_TIMEOUT_SECS * CLOCKS_PER_SEC;

	if (now - NP_LastBeating >= server_timeout) {
		NP_Warn("NutPuncher connection timed out!");
		goto error;
	}

	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		if (NutPunch_PeerAlive(i) && i != NutPunch_LocalPeer()
			&& now - NP_Peers[i].last_beating >= peer_timeout)
		{
			NP_Info("Peer %d timed out", i + 1);
			NP_KillPeer(i);
		}

	if (!NP_SendHeartbeat())
		goto sockfail;

	for (;;) {
		if (NP_LastStatus == NPS_Error) // happens after handling a GTFO
			return;
		switch (NP_ReceiveShit()) {
		case -1:
			goto sockfail;
		case 1:
			goto send;
		default:
			break;
		}
	}

send:
	if (!NP_Closing)
		goto flush;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		for (int kkk = 0; kkk < 10; kkk++) {
			static char bye[4] = {'D', 'I', 'S', 'C'};
			NP_QueueSend(i, bye, sizeof(bye), 0);
		}

flush:
	NP_PruneOutQueue(), NP_FlushOutQueue();
	return;

sockfail:
	NP_Warn("Something went wrong with your socket!");
error:
	NP_LastStatus = NPS_Error;
}

int NutPunch_Update() {
	NP_LazyInit();

	if (NP_LastStatus == NPS_Idle || NP_Sock == NUTPUNCH_INVALID_SOCKET)
		return NPS_Idle;

	NP_LastStatus = NPS_Online;
	NP_NetworkUpdate();
	NP_Trace("UPDATE OK NICE");

	if (NP_LastStatus == NPS_Error) {
		NutPunch_Disconnect();
		return NPS_Error;
	}

	return NP_LastStatus;
}

void NutPunch_Disconnect() {
	NP_Info("Disconnecting from lobby (if any)");
	if (NP_LastStatus == NPS_Online)
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

	NP_DataMessage* next = NP_QueueIn->next;
	NutPunch_Free(NP_QueueIn);

	NP_QueueIn = next;
	return source_peer;
}

static void NP_SendEx(int peer, const void* data, int data_size, int reliable) {
	NP_LazyInit();

	if (!data) {
		NP_Warn("No data?");
		return;
	}

	if (data_size > NUTPUNCH_BUFFER_SIZE - 32) {
		NP_Warn("Ignoring a huge packet");
		return;
	}

	static NP_PacketIdx packet_idx = 0;
	const NP_PacketIdx index = reliable ? ++packet_idx : 0, net_index = htonl(index);

	static char buf[NUTPUNCH_BUFFER_SIZE] = "DATA";
	char* ptr = buf + NUTPUNCH_HEADER_SIZE;

	NutPunch_Memcpy(ptr, &net_index, sizeof(net_index));
	ptr += sizeof(net_index);

	NutPunch_Memcpy(ptr, data, data_size);
	NP_QueueSend(peer, buf, NUTPUNCH_HEADER_SIZE + sizeof(index) + data_size, index);
}

void NutPunch_Send(int peer, const void* data, int size) {
	NP_SendEx(peer, data, size, 0);
}

void NutPunch_SendReliably(int peer, const void* data, int size) {
	NP_SendEx(peer, data, size, 1);
}

const NutPunch_LobbyInfo* NutPunch_GetLobby(int index) {
	NP_LazyInit();
	return index >= 0 && index < NutPunch_LobbyCount() ? &NP_Lobbies[index] : NULL;
}

int NutPunch_LobbyCount() {
	NP_LazyInit();
	static const char nully[NUTPUNCH_ID_MAX] = {0};
	for (int i = 0; i < NUTPUNCH_SEARCH_RESULTS_MAX; i++)
		if (!NutPunch_Memcmp(NP_Lobbies[i].name, nully, sizeof(nully)))
			return i;
	return NUTPUNCH_SEARCH_RESULTS_MAX;
}

int NutPunch_PeerCount() {
	int count = 0;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		count += NutPunch_PeerAlive(i);
	return count;
}

bool NutPunch_PeerAlive(int peer) {
	if (peer < 0 || peer >= NUTPUNCH_MAX_PLAYERS || NP_LastStatus != NPS_Online)
		return false;
	return NutPunch_LocalPeer() == peer || 0 != *NP_AddrPort(&NP_Peers[peer].addr);
}

int NutPunch_LocalPeer() {
	return NP_LocalPeer;
}

bool NutPunch_IsMaster() {
	return 0 != (NP_ResponseFlags & NP_R_Master);
}

const char* NutPunch_Basename(const char* path) {
	// https://github.com/toggins/Klawiatura/blob/b86d36d2b320bea87987a1a05a455e782c5a4e25/src/K_file.c#L71
	const char* s = strrchr(path, '/');
	if (!s)
		s = strrchr(path, '\\');
	return s ? s + 1 : path;
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
