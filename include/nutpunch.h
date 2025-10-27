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
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef WINVER
#define WINVER 0x0501
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

/// The default NutPuncher instance. It's public, so feel free to [ab]use it.
#define NUTPUNCH_DEFAULT_SERVER "nutpunch.schwung.us"

/// Maximum amount of players in a lobby. Not intended to be customizable.
#define NUTPUNCH_MAX_PLAYERS (16)

/// The UDP port used by the punching mediator server. Not customizable, sorry.
#define NUTPUNCH_SERVER_PORT (30001)

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

#define NUTPUNCH_HEADER_SIZE (4)
#define NUTPUNCH_ADDRESS_SIZE (19)

#define NUTPUNCH_RESPONSE_SIZE                                                                                         \
	(NUTPUNCH_HEADER_SIZE + 1 + NUTPUNCH_MAX_PLAYERS * 2 * NUTPUNCH_ADDRESS_SIZE                                   \
		+ (NUTPUNCH_MAX_PLAYERS + 1) * NUTPUNCH_MAX_FIELDS * (int)sizeof(NutPunch_Field))
#define NUTPUNCH_HEARTBEAT_SIZE                                                                                        \
	(NUTPUNCH_HEADER_SIZE + NUTPUNCH_ID_MAX + (int)sizeof(NP_HeartbeatFlagsStorage) + NUTPUNCH_ADDRESS_SIZE        \
		+ 2 * NUTPUNCH_MAX_FIELDS * (int)sizeof(NutPunch_Field))

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
	char name[NUTPUNCH_FIELD_NAME_MAX], data[NUTPUNCH_FIELD_DATA_MAX];
	uint8_t size;
} NutPunch_Field; // DOT NOT MOVE OUT, used in `NUTPUNCH_*_SIZE` constants above!!!

typedef struct {
	char name[NUTPUNCH_FIELD_NAME_MAX], value[NUTPUNCH_FIELD_DATA_MAX];
	uint8_t comparison;
} NutPunch_Filter; // public API

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
	NPE_NoSuchLobby,
	NPE_LobbyExists,
};

/// Set a custom NutPuncher server address.
void NutPunch_SetServerAddr(const char* hostname);

/// Join a lobby by its ID. If no lobby exists with this ID, spit an error status out of `NutPunch_Update()`.
int NutPunch_Join(const char*);

/// Host a lobby with specified ID. If the lobby already exists, spit an error status out of `NutPunch_Update()`.
int NutPunch_Host(const char*);

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
void* NutPunch_LobbyGet(const char* name, int* size);

/// Request your peer-specific metadata to be set. Works the same way as `NutPunch_LobbySet` otherwise.
void NutPunch_PeerSet(const char* name, int size, const void* data);

/// Request metadata for a specific peer. Works the same way as `NutPunch_LobbyGet` otherwise.
void* NutPunch_PeerGet(int peer, const char* name, int* size);

/// Check if there is a packet waiting in the receiving queue. Retrieve it with `NutPunch_NextMessage()`.
int NutPunch_HasMessage();

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

/// Return 1 if you are connected to the peer with the specified index.
///
/// If you're iterating over peers, use `NUTPUNCH_MAX_PLAYERS` as the upper index bound, and check their status using
/// this function.
int NutPunch_PeerAlive(int peer);

/// Get the local peer's index. Returns `NUTPUNCH_MAX_PLAYERS` if this fails for any reason.
int NutPunch_LocalPeer();

/// Check if we're the lobby's master.
int NutPunch_IsMaster();

/// Call this to gracefully disconnect from the lobby.
void NutPunch_Disconnect();

/// Query the lobbies list given a set of filters.
///
/// Each filter consists of a name of a field, a target value, and a comparison operator against the target value. At
/// least one filter is required; if you have nothing to compare, set a named "magic byte" in your lobby to distinguish
/// it from other games' lobbies.
///
/// For `comparison`, bitwise OR the `NPF_*` constants. For example, `NPF_Not | NPF_Eq` means "not equal to the target
/// value". Comparison is performed bytewise in a fashion similar to `memcmp`.
///
/// The lobby list is queried every call to `NutPunch_Update`, so make sure you are calling that.
void NutPunch_FindLobbies(int filterCount, const NutPunch_Filter* filters);

/// Extract the lobby IDs resulting from `NutPunch_FindLobbies`. Updates every call to `NutPunch_Update`.
const char* NutPunch_GetLobby(int index);

/// Count how many lobbies were found after `NutPunch_FindLobbies`. Updates every call to `NutPunch_Update`.
int NutPunch_LobbyCount();

/// Use this to reset the underlying socket in case of an inexplicable error.
void NutPunch_Reset();

/// Get the human-readable description of the latest error in `NutPunch_Update()`.
const char* NutPunch_GetLastError();

/// Get a file's basename (the name without directory). Used internally in `NP_Log`.
const char* NutPunch_Basename(const char*);

#ifndef NP_Log
#define NP_Log(msg, ...)                                                                                               \
	do {                                                                                                           \
		fprintf(stdout, "(%s:%d) -> " msg "\n", NutPunch_Basename(__FILE__), __LINE__, ##__VA_ARGS__);         \
		fflush(stdout);                                                                                        \
	} while (0)
#endif

#ifdef NUTPUNCH_IMPLEMENTATION

#ifdef NUTPUNCH_WINDOSE

#define _UNICODE
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

typedef uint8_t NP_IPv;
#define NP_IPv4 (0)
#define NP_IPv6 (1)

typedef uint32_t NP_PacketIdx;

typedef struct {
	struct sockaddr_storage raw;
	NP_IPv ipv;
} NP_Addr;

typedef struct NP_DataMessage {
	char* data;
	struct NP_DataMessage* next;
	NP_PacketIdx index;
	uint32_t size;
	uint8_t peer, dead;
	int16_t bounce;
} NP_DataMessage;

typedef struct {
	const char identifier[NUTPUNCH_HEADER_SIZE];
	void (*const handler)(NP_Addr, int, const uint8_t*);
	const int packetSize;
} NP_MessageTable;

#define NP_BEAT_LEN (NUTPUNCH_RESPONSE_SIZE - NUTPUNCH_HEADER_SIZE)
#define NP_LIST_LEN (NUTPUNCH_SEARCH_RESULTS_MAX * NUTPUNCH_ID_MAX)
#define NP_ACKY_LEN (sizeof(NP_PacketIdx))

#define NP_MakeHandler(name) static void name(NP_Addr peer, int size, const uint8_t* data)
NP_MakeHandler(NP_HandleShalom);
NP_MakeHandler(NP_HandleDisconnect);
NP_MakeHandler(NP_HandleGTFO);
NP_MakeHandler(NP_HandleBeat);
NP_MakeHandler(NP_HandleList);
NP_MakeHandler(NP_HandleAcky);
NP_MakeHandler(NP_HandleData);

static const NP_MessageTable NP_Messages[] = {
	{{'S', 'H', 'L', 'M'}, NP_HandleShalom,     1          },
	{{'D', 'I', 'S', 'C'}, NP_HandleDisconnect, 0          },
	{{'G', 'T', 'F', 'O'}, NP_HandleGTFO,       1          },
	{{'B', 'E', 'A', 'T'}, NP_HandleBeat,       NP_BEAT_LEN},
	{{'L', 'I', 'S', 'T'}, NP_HandleList,       NP_LIST_LEN},
	{{'A', 'C', 'K', 'Y'}, NP_HandleAcky,       NP_ACKY_LEN},
	{{'D', 'A', 'T', 'A'}, NP_HandleData,       -1         },
};

static const char* NP_LastError = NULL;
static int NP_LastErrorCode = NPE_Ok;

static int NP_InitDone = 0, NP_Closing = 0;
static int NP_LastStatus = NPS_Idle;

static char NP_LobbyId[NUTPUNCH_ID_MAX + 1] = {0};
static NP_Addr NP_Peers[NUTPUNCH_MAX_PLAYERS] = {0};
static uint8_t NP_LocalPeer = NUTPUNCH_MAX_PLAYERS;

static NP_Socket NP_Sock4 = NUTPUNCH_INVALID_SOCKET, NP_Sock6 = NUTPUNCH_INVALID_SOCKET;
static NP_Addr NP_PuncherPeer = {0};
static char NP_ServerHost[128] = {0};

static NP_DataMessage *NP_QueueIn = NULL, *NP_QueueOut = NULL;
static NutPunch_Field NP_LobbyMetadataIn[NUTPUNCH_MAX_FIELDS] = {0},
		      NP_PeerMetadataIn[NUTPUNCH_MAX_PLAYERS][NUTPUNCH_MAX_FIELDS] = {0},
		      NP_LobbyMetadataOut[NUTPUNCH_MAX_FIELDS] = {0}, NP_PeerMetadataOut[NUTPUNCH_MAX_FIELDS] = {0};

static int NP_Querying = 0;
static NutPunch_Filter NP_Filters[NUTPUNCH_SEARCH_FILTERS_MAX] = {0};

static char NP_LobbyNames[NUTPUNCH_SEARCH_RESULTS_MAX][NUTPUNCH_ID_MAX + 1] = {0};
static char* NP_Lobbies[NUTPUNCH_SEARCH_RESULTS_MAX] = {0};

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

static void* NP_AddrRaw(NP_Addr* addr) {
	if (addr->ipv == NP_IPv6)
		return &((struct sockaddr_in6*)&addr->raw)->sin6_addr;
	else
		return &((struct sockaddr_in*)&addr->raw)->sin_addr;
}

static uint16_t* NP_AddrFamily(NP_Addr* addr) {
	if (addr->ipv == NP_IPv6)
		return (uint16_t*)&((struct sockaddr_in6*)&addr->raw)->sin6_family;
	else
		return (uint16_t*)&((struct sockaddr_in*)&addr->raw)->sin_family;
}

static uint16_t* NP_AddrPort(NP_Addr* addr) {
	if (addr->ipv == NP_IPv6)
		return &((struct sockaddr_in6*)&addr->raw)->sin6_port;
	else
		return &((struct sockaddr_in*)&addr->raw)->sin_port;
}

static int NP_AddrEq(const NP_Addr a, const NP_Addr b) {
	const int size = a.ipv == NP_IPv6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
	return a.ipv == b.ipv && !NutPunch_Memcmp(&a.raw, &b.raw, size);
}

static int NP_AddrNull(const NP_Addr addr) {
	static const char nulladdr[16] = {0};
	return !NutPunch_Memcmp(&addr.raw, nulladdr, 16);
}

static void NP_CleanupPackets(NP_DataMessage** queue) {
	while (*queue != NULL) {
		NP_DataMessage* ptr = *queue;
		*queue = ptr->next;
		NutPunch_Free(ptr->data);
		NutPunch_Free(ptr);
	}
}

static void NP_NukeLobbyData() {
	NP_Closing = NP_Querying = 0, NP_ResponseFlags = 0;
	NP_LocalPeer = NUTPUNCH_MAX_PLAYERS;
	NP_Memzero(NP_LobbyMetadataIn), NP_Memzero(NP_PeerMetadataIn);
	NP_Memzero(NP_LobbyMetadataOut), NP_Memzero(NP_PeerMetadataOut);
	NP_Memzero(NP_Peers), NP_Memzero(NP_Filters);
	NP_CleanupPackets(&NP_QueueIn), NP_CleanupPackets(&NP_QueueOut);
}

static void NP_NukeRemote() {
	NP_LobbyId[0] = 0, NP_HeartbeatFlags = 0;
	NP_Memzero2(&NP_PuncherPeer), NP_Memzero(NP_Peers);
	NP_Memzero(NP_PeerMetadataIn), NP_Memzero(NP_Filters);
	NP_LastStatus = NPS_Idle;
}

static void NP_NukeSocket(NP_Socket* sock) {
	if (*sock == NUTPUNCH_INVALID_SOCKET)
		return;
#ifdef NUTPUNCH_WINDOSE
	closesocket(*sock);
#else
	close(*sock);
#endif
	*sock = NUTPUNCH_INVALID_SOCKET;
}

static void NP_ResetImpl() {
	NP_NukeRemote();
	NP_NukeLobbyData();
	NP_NukeSocket(&NP_Sock4);
	NP_NukeSocket(&NP_Sock6);
}

static void NP_LazyInit() {
	if (NP_InitDone)
		return;
	NP_InitDone = 1;

#ifdef NUTPUNCH_WINDOSE
	{
		WSADATA bitch = {0};
		WSAStartup(MAKEWORD(2, 2), &bitch);
	}
#endif

	for (int i = 0; i < NUTPUNCH_SEARCH_RESULTS_MAX; i++)
		NP_Lobbies[i] = NP_LobbyNames[i];
	NP_ResetImpl();

	NP_Log(".-------------------------------------------------------------.");
	NP_Log("| For troubleshooting multiplayer connectivity, please visit: |");
	NP_Log("|    https://github.com/Schwungus/nutpunch#troubleshooting    |");
	NP_Log("'-------------------------------------------------------------'");
}

void NutPunch_Reset() {
	NP_LazyInit();
	NP_ResetImpl();
}

static void NP_PrintError() {
	if (NP_LastErrorCode)
		NP_Log("WARN: %s (error code: %d)", NP_LastError, NP_LastErrorCode);
	else
		NP_Log("WARN: %s", NP_LastError);
}

static int NP_GetAddrInfo(NP_Addr* out, const char* host, uint16_t port, NP_IPv ipv) {
	NP_LazyInit();

	out->ipv = ipv;
	NP_Memzero2(&out->raw);

	if (ipv == NP_IPv6) {
		((struct sockaddr_in6*)&out->raw)->sin6_family = AF_INET6;
		((struct sockaddr_in6*)&out->raw)->sin6_port = htons(port);
	} else {
		((struct sockaddr_in*)&out->raw)->sin_family = AF_INET;
		((struct sockaddr_in*)&out->raw)->sin_port = htons(port);
	}

	if (host == NULL)
		return 1;

	struct addrinfo *result = NULL, hints = {0};
	hints.ai_family = ipv == NP_IPv6 ? AF_INET6 : AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_flags = AI_PASSIVE;

	static char fmt[8] = {0};
	NP_Memzero(fmt), snprintf(fmt, sizeof(fmt), "%d", port);

	if (getaddrinfo(host, fmt, &hints, &result)) {
		NP_LastError = "Failed to get NutPuncher server address info";
		NP_LastErrorCode = NP_SockError();
		NP_PrintError();
		return 0;
	}
	if (result == NULL)
		return 0;

	NP_Memzero2(&out->raw);
	NutPunch_Memcpy(&out->raw, result->ai_addr, result->ai_addrlen);
	freeaddrinfo(result);
	return 1;
}

static NP_Addr NP_ResolveAddr(const char* host, uint16_t port) {
	NP_Addr out = {0};
	if (!NP_GetAddrInfo(&out, host, port, NP_IPv6))
		NP_GetAddrInfo(&out, host, port, NP_IPv4);
	return out;
}

void NutPunch_SetServerAddr(const char* hostname) {
	if (hostname == NULL)
		NP_ServerHost[0] = 0;
	else
		snprintf(NP_ServerHost, sizeof(NP_ServerHost), "%s", hostname);
}

static int NP_FieldNameSize(const char* name) {
	for (int i = 0; i < NUTPUNCH_FIELD_NAME_MAX; i++)
		if (!name[i])
			return i;
	return NUTPUNCH_FIELD_NAME_MAX;
}

static void* NP_GetMetadataFrom(NutPunch_Field* fields, const char* name, int* size) {
	static char buf[NUTPUNCH_FIELD_DATA_MAX] = {0};
	NP_Memzero(buf);

	int nameSize = NP_FieldNameSize(name);
	if (!nameSize)
		goto none;

	for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
		NutPunch_Field* ptr = &fields[i];
		if (nameSize != NP_FieldNameSize(ptr->name) || NutPunch_Memcmp(ptr->name, name, nameSize))
			continue;
		NutPunch_Memcpy(buf, ptr->data, ptr->size);
		if (size != NULL)
			*size = ptr->size;
		return buf;
	}
none:
	if (size != NULL)
		*size = 0;
	return NULL;
}

void* NutPunch_LobbyGet(const char* name, int* size) {
	return NP_GetMetadataFrom(NP_LobbyMetadataIn, name, size);
}

void* NutPunch_PeerGet(int peer, const char* name, int* size) {
	if (!NutPunch_PeerAlive(peer))
		goto none;
	if (peer < 0 || peer >= NUTPUNCH_MAX_PLAYERS)
		goto none;
	return NP_GetMetadataFrom(NP_PeerMetadataIn[peer], name, size);
none:
	if (size != NULL)
		*size = 0;
	return NULL;
}

static void NP_SetMetadataIn(NutPunch_Field* fields, const char* name, int dataSize, const void* data) {
	int nameSize = NP_FieldNameSize(name);
	if (!nameSize)
		return;

	if (dataSize < 1) {
		NP_Log("Invalid metadata field size!");
		return;
	} else if (dataSize > NUTPUNCH_FIELD_DATA_MAX) {
		NP_Log("WARN: trimming metadata field from %d to %d bytes", dataSize, NUTPUNCH_FIELD_DATA_MAX);
		dataSize = NUTPUNCH_FIELD_DATA_MAX;
	}

	static const NutPunch_Field nullfield = {0};
	for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
		NutPunch_Field* field = &fields[i];

		if (!NutPunch_Memcmp(field, &nullfield, sizeof(nullfield)))
			goto set;
		if (NP_FieldNameSize(field->name) == nameSize && !NutPunch_Memcmp(field->name, name, nameSize))
			goto set;
		continue;

	set:
		NP_Memzero(field->name), NutPunch_Memcpy(field->name, name, nameSize);
		NP_Memzero(field->data), NutPunch_Memcpy(field->data, data, dataSize);
		field->size = dataSize;
		return;
	}
}

void NutPunch_PeerSet(const char* name, int size, const void* data) {
	NP_SetMetadataIn(NP_PeerMetadataOut, name, size, data);
}

void NutPunch_LobbySet(const char* name, int size, const void* data) {
	NP_SetMetadataIn(NP_LobbyMetadataOut, name, size, data);
}

static void NP_ExpectNutpuncher() {
	if (!NP_ServerHost[0]) {
		NutPunch_SetServerAddr(NUTPUNCH_DEFAULT_SERVER);
		NP_Log("Connecting to public NutPunch instance because no address was specified");
	}
}

static int NP_MakeNonblocking(NP_Socket sock) {
#ifdef NUTPUNCH_WINDOSE
	u_long argp = 1;
	return ioctlsocket(sock, FIONBIO, &argp);
#else
	return fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
#endif
}

static int NP_BindSocket(NP_IPv ipv) {
	NP_LazyInit();

	NP_Addr local = {0};
	NP_Socket* sock = ipv == NP_IPv6 ? &NP_Sock6 : &NP_Sock4;
	NP_NukeSocket(sock);

	*sock = socket(ipv == NP_IPv6 ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (*sock == NUTPUNCH_INVALID_SOCKET) {
		if (ipv == NP_IPv6)
			goto v6_optional;
		else {
			NP_LastError = "Failed to create the underlying UDP socket";
			goto fail;
		}
	}

	if (NP_MakeNonblocking(*sock) < 0) {
		NP_LastError = "Failed to set socket to non-blocking mode";
		goto fail;
	}

	NP_GetAddrInfo(&local, NULL, 0, ipv);
	if (!bind(*sock, (struct sockaddr*)&local.raw, sizeof(local.raw))) {
		NP_ExpectNutpuncher();
		NP_PuncherPeer = NP_ResolveAddr(NP_ServerHost, NUTPUNCH_SERVER_PORT);
		return 1;
	}

	if (ipv == NP_IPv6) {
	v6_optional:
		NP_Log("WARN: failed to bind an IPv6 socket");
		NP_NukeSocket(sock);
		return 1;
	}

	NP_LastError = "Failed to bind the UDP socket";
fail:
	NP_LastStatus = NPS_Error;
	NP_LastErrorCode = NP_SockError();
	NP_PrintError();
	NutPunch_Reset();
	return 0;
}

static int NutPunch_Connect(const char* lobbyId) {
	NP_LazyInit();
	NP_NukeLobbyData();
	NP_ExpectNutpuncher();

	if (!NP_BindSocket(NP_IPv6) || !NP_BindSocket(NP_IPv4))
		goto fail;

	NP_LastStatus = NPS_Online;
	if (lobbyId != NULL) {
		NP_Memzero(NP_LobbyId);
		snprintf(NP_LobbyId, sizeof(NP_LobbyId), "%s", lobbyId);
	}
	return 1;

fail:
	NutPunch_Reset();
	NP_LastStatus = NPS_Error;
	return 0;
}

int NutPunch_Host(const char* lobbyId) {
	NP_HeartbeatFlags = NP_HB_Join | NP_HB_Create;
	return NutPunch_Connect(lobbyId);
}

int NutPunch_Join(const char* lobbyId) {
	NP_HeartbeatFlags = NP_HB_Join;
	return NutPunch_Connect(lobbyId);
}

void NutPunch_FindLobbies(int filterCount, const NutPunch_Filter* filters) {
	if (filterCount < 1)
		return;
	else if (filterCount > NUTPUNCH_SEARCH_FILTERS_MAX)
		filterCount = NUTPUNCH_SEARCH_FILTERS_MAX;
	NP_Querying = NutPunch_Connect(NULL);
	NutPunch_Memcpy(NP_Filters, filters, filterCount * sizeof(*filters));
}

void NutPunch_Disconnect() {
	NP_Closing = 1;
	NutPunch_Update();
	NutPunch_Reset();
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

static void NP_KillPeer(int peer) {
	NutPunch_Memset(NP_Peers + peer, 0, sizeof(*NP_Peers));
}

static void NP_QueueSend(int peer, const void* data, int size, NP_PacketIdx index) {
	if (!NutPunch_PeerAlive(peer) || NutPunch_LocalPeer() == peer)
		return;
	if (size > NUTPUNCH_BUFFER_SIZE) {
		NP_Log("Ignoring a huge packet");
		return;
	}

	NP_DataMessage* next = NP_QueueOut;
	NP_QueueOut = (NP_DataMessage*)NutPunch_Malloc(sizeof(*next));
	NP_QueueOut->next = next, NP_QueueOut->peer = peer;
	NP_QueueOut->size = size, NP_QueueOut->bounce = index ? 0 : -1;
	NP_QueueOut->index = index, NP_QueueOut->dead = 0;
	NP_QueueOut->data = (char*)NutPunch_Malloc(size);
	NutPunch_Memcpy(NP_QueueOut->data, data, size);
}

NP_MakeHandler(NP_HandleShalom) {
	if (NP_AddrEq(peer, NP_PuncherPeer))
		return;
	const uint8_t idx = *data;
	if (idx < NUTPUNCH_MAX_PLAYERS)
		NP_Peers[idx] = peer;
}

NP_MakeHandler(NP_HandleDisconnect) {
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		if (NP_AddrEq(peer, NP_Peers[i]))
			NP_KillPeer(i);
}

NP_MakeHandler(NP_HandleGTFO) {
	if (!NP_AddrEq(peer, NP_PuncherPeer))
		return;

	NutPunch_Reset();
	NP_LastStatus = NPS_Error;
	NP_LastErrorCode = *data;

	switch (NP_LastErrorCode) {
	case NPE_NoSuchLobby:
		NP_LastError = "Lobby doesn't exist";
		break;
	case NPE_LobbyExists:
		NP_LastError = "Lobby already exists";
		break;
	case NPE_Ok:
		NP_LastError = "wtf bro";
		break;
	default:
		NP_LastError = "Unidentified error";
		break;
	}
}

static void NP_SayShalom(const uint8_t* data) {
	NP_Addr peer = {0};
	peer.ipv = *data++;

	*NP_AddrFamily(&peer) = (peer.ipv == NP_IPv6 ? AF_INET6 : AF_INET);
	NutPunch_Memcpy(NP_AddrRaw(&peer), data, (peer.ipv == NP_IPv6 ? 16 : 4));
	data += 16;

	if (NP_AddrNull(peer))
		return;

	uint16_t* port = NP_AddrPort(&peer);
	NutPunch_Memcpy(port, data, 2);
	data += 2;

	if (!*port)
		return;

	static uint8_t shalom[] = "SHLM";
	shalom[NUTPUNCH_HEADER_SIZE] = (uint8_t)NP_LocalPeer;

	const NP_Socket sock = peer.ipv == NP_IPv6 ? NP_Sock6 : NP_Sock4;
	if (sock != NUTPUNCH_INVALID_SOCKET)
		sendto(sock, (char*)shalom, sizeof(shalom), 0, (struct sockaddr*)&peer.raw, sizeof(peer.raw));
}

NP_MakeHandler(NP_HandleBeat) {
	if (!NP_AddrEq(peer, NP_PuncherPeer))
		return;

	NP_LocalPeer = NUTPUNCH_MAX_PLAYERS;
	const int metaSize = NUTPUNCH_MAX_FIELDS * sizeof(NutPunch_Field);
	const ptrdiff_t stride = NUTPUNCH_ADDRESS_SIZE + NUTPUNCH_ADDRESS_SIZE + metaSize;

	NP_ResponseFlags = *data++;
	for (uint8_t i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		const uint8_t *ptr = data + i * stride + NUTPUNCH_ADDRESS_SIZE, nulladdr[16] = {0};
		if (!NutPunch_Memcmp(ptr + 1, nulladdr, 16) && *(uint16_t*)(ptr + 17)) {
			NP_LocalPeer = i;
			break;
		}
	}
	if (NP_LocalPeer == NUTPUNCH_MAX_PLAYERS)
		return;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		NP_SayShalom(data), data += NUTPUNCH_ADDRESS_SIZE; // private addr
		NP_SayShalom(data), data += NUTPUNCH_ADDRESS_SIZE; // public addr
		NutPunch_Memcpy(NP_PeerMetadataIn[i], data, metaSize), data += metaSize;
	}
	NutPunch_Memcpy(NP_LobbyMetadataIn, data, metaSize);
}

NP_MakeHandler(NP_HandleList) {
	if (!NP_AddrEq(peer, NP_PuncherPeer))
		return;
	for (int i = 0; i < NUTPUNCH_SEARCH_RESULTS_MAX; i++) {
		NutPunch_Memcpy(NP_Lobbies[i], data, NUTPUNCH_ID_MAX);
		NP_Lobbies[i][NUTPUNCH_ID_MAX] = '\0';
		data += NUTPUNCH_ID_MAX;
	}
}

NP_MakeHandler(NP_HandleData) {
	int peerIdx = NUTPUNCH_MAX_PLAYERS;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		if (NP_AddrEq(peer, NP_Peers[i])) {
			peerIdx = i;
			break;
		}
	}
	if (peerIdx == NUTPUNCH_MAX_PLAYERS)
		return;

	const NP_PacketIdx netIndex = *(NP_PacketIdx*)data, index = ntohl(netIndex);
	size -= sizeof(index), data += sizeof(index);

	if (index) {
		static char ack[NUTPUNCH_HEADER_SIZE + sizeof(netIndex)] = "ACKY";
		NutPunch_Memcpy(ack + NUTPUNCH_HEADER_SIZE, &netIndex, sizeof(netIndex));
		NP_QueueSend(peerIdx, ack, sizeof(ack), 0);
	}

	NP_DataMessage* next = NP_QueueIn;
	NP_QueueIn = (NP_DataMessage*)NutPunch_Malloc(sizeof(*next));
	NP_QueueIn->data = (char*)NutPunch_Malloc(size);
	NutPunch_Memcpy(NP_QueueIn->data, data, size);
	NP_QueueIn->peer = peerIdx, NP_QueueIn->size = size;
	NP_QueueIn->next = next;
}

NP_MakeHandler(NP_HandleAcky) {
	NP_PacketIdx index = ntohl(*(NP_PacketIdx*)data);
	for (NP_DataMessage* ptr = NP_QueueOut; ptr != NULL; ptr = ptr->next)
		if (ptr->index == index) {
			ptr->dead = 1;
			return;
		}
}

static int NP_SendHeartbeat() {
	const NP_Socket sock = NP_PuncherPeer.ipv == NP_IPv6 ? NP_Sock6 : NP_Sock4;
	if (sock == NUTPUNCH_INVALID_SOCKET)
		return 1;

	static char heartbeat[NUTPUNCH_HEARTBEAT_SIZE] = {0};
	NP_Memzero(heartbeat);

	char* ptr = heartbeat;
	if (NP_Querying) {
		NutPunch_Memcpy(ptr, "LIST", NUTPUNCH_HEADER_SIZE), ptr += NUTPUNCH_HEADER_SIZE;
		NutPunch_Memcpy(ptr, NP_Filters, sizeof(NP_Filters)), ptr += sizeof(NP_Filters);
	} else {
		NutPunch_Memcpy(ptr, "JOIN", NUTPUNCH_HEADER_SIZE), ptr += NUTPUNCH_HEADER_SIZE;
		NutPunch_Memcpy(ptr, NP_LobbyId, NUTPUNCH_ID_MAX), ptr += NUTPUNCH_ID_MAX;

		// TODO: make sure to correct endianness when multibyte flags become a thing.
		*(NP_HeartbeatFlagsStorage*)ptr = NP_HeartbeatFlags, ptr += sizeof(NP_HeartbeatFlags);

		struct sockaddr_storage addr = {0};
		socklen_t addr_size = sizeof(addr);
		getsockname(sock, (struct sockaddr*)&addr, &addr_size);

		NP_Addr np_addr;
		np_addr.raw = addr;
		np_addr.ipv = NP_PuncherPeer.ipv;

		*ptr++ = *(char*)&np_addr.ipv;
		NutPunch_Memcpy(ptr, NP_AddrRaw(&np_addr), sock == NP_Sock6 ? 16 : 4), ptr += 16;
		NutPunch_Memcpy(ptr, NP_AddrPort(&np_addr), 2), ptr += 2; // TODO: `htons`?

		const int metaSize = NUTPUNCH_MAX_FIELDS * sizeof(NutPunch_Field);
		NutPunch_Memcpy(ptr, NP_PeerMetadataOut, metaSize), ptr += metaSize;
		NutPunch_Memcpy(ptr, NP_LobbyMetadataOut, metaSize), ptr += metaSize;
	}

	size_t length = ptr - heartbeat;
	int status = sendto(
		sock, heartbeat, (int)length, 0, (struct sockaddr*)&NP_PuncherPeer.raw, sizeof(NP_PuncherPeer.raw));
	if (status < 0 && NP_SockError() != NP_WouldBlock && NP_SockError() != NP_ConnReset) {
		NP_LastError = "Failed to send heartbeat to NutPuncher";
		return 0;
	}
	return 1;
}

static int NP_ReceiveShit(NP_IPv ipv) {
	NP_Socket* sock = ipv == NP_IPv6 ? &NP_Sock6 : &NP_Sock4;
	if (*sock == NUTPUNCH_INVALID_SOCKET)
		return 1;

	struct sockaddr_storage addr = {0};
	socklen_t addrSize = sizeof(addr);
	NP_Memzero2(&addr);

	static char buf[NUTPUNCH_BUFFER_SIZE] = {0};
	int size = recvfrom(*sock, buf, sizeof(buf), 0, (struct sockaddr*)&addr, &addrSize);
	if (size < 0) {
		if (NP_SockError() == NP_WouldBlock)
			return 1;
		if (NP_SockError() == NP_ConnReset)
			return 0;
		NP_LastError = "Failed to receive from NutPuncher";
		return -1;
	}

	if (!size) // graceful disconnection
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			if (!NutPunch_Memcmp(&addr, &NP_Peers[i].raw, sizeof(addr))) {
				NP_KillPeer(i);
				return 0;
			}

	if (size < NUTPUNCH_HEADER_SIZE)
		return 0;
	size -= NUTPUNCH_HEADER_SIZE;

	for (int i = 0; i < sizeof(NP_Messages) / sizeof(*NP_Messages); i++) {
		const NP_MessageTable msg = NP_Messages[i];
		if (!NutPunch_Memcmp(buf, msg.identifier, NUTPUNCH_HEADER_SIZE)
			&& (msg.packetSize < 0 || size == msg.packetSize))
		{
			NP_Addr peer = {.raw = addr, .ipv = ipv};
			msg.handler(peer, size, (uint8_t*)(buf + NUTPUNCH_HEADER_SIZE));
			return 0;
		}
	}

	return 0;
}

static void NP_PruneOutQueue() {
findNext:
	for (NP_DataMessage* ptr = NP_QueueOut; ptr != NULL; ptr = ptr->next) {
		if (!ptr->dead)
			continue;

		for (NP_DataMessage* other = NP_QueueOut; other != NULL; other = other->next)
			if (other->next == ptr) {
				other->next = ptr->next;
				NutPunch_Free(ptr->data);
				NutPunch_Free(ptr);
				goto findNext;
			}

		if (ptr == NP_QueueOut)
			NP_QueueOut = ptr->next;
		else
			NP_QueueOut->next = ptr->next;
		NutPunch_Free(ptr->data), NutPunch_Free(ptr);
		goto findNext;
	}
}

static void NP_FlushOutQueue() {
	for (NP_DataMessage* cur = NP_QueueOut; cur != NULL; cur = cur->next) {
		if (!NutPunch_PeerAlive(cur->peer)) {
			cur->dead = 1;
			continue;
		}

		// Send & pop normally since a bounce of -1 makes it an unreliable packet.
		if (cur->bounce < 0)
			cur->dead = 1;
		// Otherwise, check if it's about to bounce, in order to resend it.
		else if (cur->bounce > 0)
			if (--cur->bounce > 0)
				continue;
		cur->bounce = NUTPUNCH_BOUNCE_TICKS;

		const NP_Addr peer = NP_Peers[cur->peer];
		const NP_Socket sock = peer.ipv == NP_IPv6 ? NP_Sock6 : NP_Sock4;
		if (sock == NUTPUNCH_INVALID_SOCKET) {
			cur->dead = 1;
			continue;
		}

		int result = sendto(sock, cur->data, (int)cur->size, 0, (struct sockaddr*)&peer.raw, sizeof(peer.raw));
		if (result > 0 || NP_SockError() == NP_WouldBlock)
			continue;

		if (!result || NP_SockError() == NP_ConnReset)
			NP_KillPeer(cur->peer);
		else {
			NP_LastError = "Failed to send to peer";
			NP_LastErrorCode = NP_SockError();
			NP_PrintError();
			NP_NukeLobbyData();
			return;
		}
	}
}

static int NP_RealUpdate() {
	static const int socks[] = {NP_IPv6, NP_IPv4, -999};
	const int* ipVer = socks;

	if (!NP_SendHeartbeat())
		goto sockFail;

	while (*ipVer >= 0) {
		switch (NP_ReceiveShit(*ipVer)) {
		case -1:
			goto sockFail;
		case 1:
			ipVer++;
		default:
			break;
		}
	}

	static char bye[4] = {'D', 'I', 'S', 'C'};
	if (!NP_Closing)
		goto flush;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		for (int kkk = 0; kkk < 10; kkk++)
			NP_QueueSend(i, bye, sizeof(bye), 0);

flush:
	NP_PruneOutQueue(), NP_FlushOutQueue();
	return NPS_Online;

sockFail:
	NP_LastErrorCode = NP_SockError();
	NP_NukeLobbyData();
	return NPS_Error;
}

int NutPunch_Update() {
	NP_LazyInit();
	int socks_dead = NP_Sock4 == NUTPUNCH_INVALID_SOCKET && NP_Sock6 == NUTPUNCH_INVALID_SOCKET;
	if (NP_LastStatus == NPS_Idle || socks_dead)
		return NPS_Idle;
	if (NP_LastStatus == NPS_Error)
		NutPunch_Reset();
	return (NP_LastStatus = NP_RealUpdate());
}

int NutPunch_HasMessage() {
	return NP_QueueIn != NULL;
}

int NutPunch_NextMessage(void* out, int* size) {
	if (*size < NP_QueueIn->size) {
		NP_Log("WARN: not enough memory allocated to copy the next packet");
		return NUTPUNCH_MAX_PLAYERS;
	}

	if (size != NULL)
		*size = (int)NP_QueueIn->size;
	NutPunch_Memcpy(out, NP_QueueIn->data, NP_QueueIn->size);
	NutPunch_Free(NP_QueueIn->data);

	int sourcePeer = NP_QueueIn->peer;
	if (sourcePeer > NUTPUNCH_MAX_PLAYERS)
		sourcePeer = NUTPUNCH_MAX_PLAYERS;

	NP_DataMessage* next = NP_QueueIn->next;
	NutPunch_Free(NP_QueueIn);

	NP_QueueIn = next;
	return sourcePeer;
}

static void NP_SendEx(int peer, const void* data, int dataSize, int reliable) {
	static char buf[NUTPUNCH_BUFFER_SIZE] = "DATA";
	NP_LazyInit();

	if (dataSize > NUTPUNCH_BUFFER_SIZE - 32) {
		NP_Log("Ignoring a huge packet");
		return;
	}

	static NP_PacketIdx packetIdx = 0;
	const NP_PacketIdx index = reliable ? ++packetIdx : 0, netIndex = htonl(index);

	char* ptr = buf + NUTPUNCH_HEADER_SIZE;
	NutPunch_Memcpy(ptr, &netIndex, sizeof(netIndex));
	ptr += sizeof(netIndex);

	NutPunch_Memcpy(ptr, data, dataSize);
	NP_QueueSend(peer, buf, NUTPUNCH_HEADER_SIZE + sizeof(index) + dataSize, index);
}

void NutPunch_Send(int peer, const void* data, int size) {
	NP_SendEx(peer, data, size, 0);
}

void NutPunch_SendReliably(int peer, const void* data, int size) {
	NP_SendEx(peer, data, size, 1);
}

const char* NutPunch_GetLobby(int index) {
	NP_LazyInit();
	return index < NutPunch_LobbyCount() ? NP_Lobbies[index] : NULL;
}

int NutPunch_LobbyCount() {
	static const char nully[NUTPUNCH_ID_MAX + 1] = {0};
	NP_LazyInit();
	for (int i = 0; i < NUTPUNCH_SEARCH_RESULTS_MAX; i++)
		if (!NutPunch_Memcmp(NP_Lobbies[i], nully, sizeof(nully)))
			return i;
	return NUTPUNCH_SEARCH_RESULTS_MAX;
}

int NutPunch_PeerCount() {
	int count = 0;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		count += NutPunch_PeerAlive(i);
	return count;
}

int NutPunch_PeerAlive(int peer) {
	if (NP_LastStatus != NPS_Online)
		return 0;
	if (NutPunch_LocalPeer() == peer)
		return 1;
	return 0 != *NP_AddrPort(NP_Peers + peer);
}

int NutPunch_LocalPeer() {
	return NP_LocalPeer;
}

int NutPunch_IsMaster() {
	return 0 != (NP_ResponseFlags & NP_R_Master);
}

const char* NutPunch_Basename(const char* path) {
	// https://github.com/toggins/Klawiatura/blob/b86d36d2b320bea87987a1a05a455e782c5a4e25/src/K_file.c#L71
	const char* s = strrchr(path, '/');
	if (!s)
		s = strrchr(path, '\\');
	return s ? s + 1 : path;
}

#endif

#ifdef __cplusplus
}
#endif

#endif
