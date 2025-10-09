#pragma once

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
#define NUTPUNCH_BUFFER_SIZE (512000)

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

#define NUTPUNCH_RESPONSE_SIZE                                                                                         \
	(NUTPUNCH_HEADER_SIZE + 1 + NUTPUNCH_MAX_PLAYERS * 19                                                          \
		+ (NUTPUNCH_MAX_PLAYERS + 1) * NUTPUNCH_MAX_FIELDS * (int)sizeof(NutPunch_Field))
#define NUTPUNCH_HEARTBEAT_SIZE                                                                                        \
	(NUTPUNCH_HEADER_SIZE + NUTPUNCH_ID_MAX + (int)sizeof(NP_HeartbeatFlagsStorage)                                \
		+ NP_MetaCount * NUTPUNCH_MAX_FIELDS * (int)sizeof(NutPunch_Field))

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum {
	NP_Status_Error,
	NP_Status_Idle,
	NP_Status_Online,
};

typedef struct {
	char name[NUTPUNCH_FIELD_NAME_MAX], data[NUTPUNCH_FIELD_DATA_MAX];
	uint8_t size;
} NutPunch_Field;

typedef struct {
	char name[NUTPUNCH_FIELD_NAME_MAX], value[NUTPUNCH_FIELD_DATA_MAX];
	int8_t comparison;
} NutPunch_Filter;

enum {
	NP_MetaPeer,
	NP_MetaLobby,
	NP_MetaCount,
};

enum {
	NP_Err_Ok,
	NP_Err_NoSuchLobby,
	NP_Err_LobbyExists,
};

/// Set a custom NutPuncher server address.
void NutPunch_SetServerAddr(const char* hostname);

/// Join a lobby by its ID. If no lobby exists with this ID, spit an error status out of `NutPunch_Update()`.
int NutPunch_Join(const char*);

/// Host a lobby with specified ID. If the lobby already exists, spit an error status out of `NutPunch_Update()`.
int NutPunch_Host(const char*);

/// Call this at the end of your program to run semi-important cleanup.
void NutPunch_Cleanup();

/// Call this every frame to update nutpunch. Returns one of the `NP_Status_*` constants.
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

/// Retrieve the next packet in the receiving queue. Return the index of the peer who sent it.
int NutPunch_NextMessage(void* out, int* size);

/// Send data to specified peer. For reliable packet delivery, use `NutPunch_SendReliably`.
void NutPunch_Send(int peer, const void* data, int size);

/// Send data to specified peer expecting them to acknowledge the fact of reception.
void NutPunch_SendReliably(int peer, const void* data, int size);

/// Count how many "live" peers we have a route to, including our local peer.
///
/// Do not use this as an upper bound for iterating over peers. They can come in any order and with gaps in-between.
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

/// Query the lobbies given a set of filters. Make sure to call `NutPunch_SetServerAddr` beforehand.
///
/// For `comparison`, use `0` for an exact value match, `-1` for "less than the server's", and `1` vice versa.
///
/// The lobby list is queried every call to `NutPunch_Update`, so make sure you aren't skipping it.
void NutPunch_FindLobbies(int filterCount, const NutPunch_Filter* filters);

/// Reap the fruits of `NutPunch_FindLobbies`. Updates every call to `NutPunch_Update`.
const char* NutPunch_GetLobby(int index);

/// Count how many lobbies were found after `NutPunch_FindLobbies`. Updates every call to `NutPunch_Update`.
int NutPunch_LobbyCount();

/// Use this to reset the underlying socket in case of an inexplicable error.
void NutPunch_Reset();

/// Get the human-readable description of the latest error in `NutPunch_Update()`.
const char* NutPunch_GetLastError();

#define NP_Log(...)                                                                                                    \
	do {                                                                                                           \
		fprintf(stdout, "[NP] " __VA_ARGS__);                                                                  \
		fprintf(stdout, "\n");                                                                                 \
		fflush(stdout);                                                                                        \
	} while (0)

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

typedef uint8_t NP_IPv;
#define NP_IPv4 (0)
#define NP_IPv6 (1)

typedef uint32_t NP_PacketIdx;

#define NP_AddrPort(addr)                                                                                              \
	((addr).ipv == NP_IPv6 ? &((struct sockaddr_in6*)&(addr))->sin6_port                                           \
			       : &((struct sockaddr_in*)&(addr))->sin_port)
typedef struct {
	struct sockaddr_storage value;
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
	int packetSize;
} NP_MessageTable;

#define NP_JOIN_LEN (NUTPUNCH_RESPONSE_SIZE - NUTPUNCH_HEADER_SIZE)
#define NP_LIST_LEN (NUTPUNCH_SEARCH_RESULTS_MAX * NUTPUNCH_ID_MAX)
#define NP_ACKY_LEN (sizeof(NP_PacketIdx))

#define NP_MakeHandler(name) static void name(NP_Addr peer, int size, const uint8_t* data)
NP_MakeHandler(NP_HandleIntro);
NP_MakeHandler(NP_HandleDisconnect);
NP_MakeHandler(NP_HandleGTFO);
NP_MakeHandler(NP_HandleJoin);
NP_MakeHandler(NP_HandleList);
NP_MakeHandler(NP_HandleAcky);
NP_MakeHandler(NP_HandleData);

static const NP_MessageTable NP_Messages[] = {
	{{'I', 'N', 'T', 'R'}, NP_HandleIntro,      1          },
	{{'D', 'I', 'S', 'C'}, NP_HandleDisconnect, 0          },
	{{'G', 'T', 'F', 'O'}, NP_HandleGTFO,       1          },
	{{'J', 'O', 'I', 'N'}, NP_HandleJoin,       NP_JOIN_LEN},
	{{'L', 'I', 'S', 'T'}, NP_HandleList,       NP_LIST_LEN},
	{{'A', 'C', 'K', 'Y'}, NP_HandleAcky,       NP_ACKY_LEN},
	{{'D', 'A', 'T', 'A'}, NP_HandleData,       -1         },
};

static const char* NP_LastError = NULL;
static int NP_LastErrorCode = 0;

static int NP_InitDone = 0, NP_Closing = 0;
static int NP_LastStatus = NP_Status_Idle;

static char NP_LobbyId[NUTPUNCH_ID_MAX + 1] = {0};
static NP_Addr NP_Peers[NUTPUNCH_MAX_PLAYERS] = {0};
static uint8_t NP_LocalPeer = NUTPUNCH_MAX_PLAYERS;

static NP_Socket NP_Sock4 = NUTPUNCH_INVALID_SOCKET, NP_Sock6 = NUTPUNCH_INVALID_SOCKET;
static NP_Addr NP_PuncherPeer = {0};
static char NP_ServerHost[128] = {0};

static NP_DataMessage *NP_QueueIn = NULL, *NP_QueueOut = NULL;
static NutPunch_Field NP_LobbyMetadata[NUTPUNCH_MAX_FIELDS] = {0},
		      NP_PeerMetadata[NUTPUNCH_MAX_PLAYERS][NUTPUNCH_MAX_FIELDS] = {0},
		      NP_MetadataOut[NP_MetaCount][NUTPUNCH_MAX_FIELDS] = {0};

static int NP_Querying = 0;
static NutPunch_Filter NP_Filters[NUTPUNCH_SEARCH_FILTERS_MAX] = {0};

static char NP_LobbyNames[NUTPUNCH_SEARCH_RESULTS_MAX][NUTPUNCH_ID_MAX + 1] = {0};
static char* NP_Lobbies[NUTPUNCH_SEARCH_RESULTS_MAX] = {0};

typedef uint8_t NP_HeartbeatFlagsStorage;
static NP_HeartbeatFlagsStorage NP_HeartbeatFlags = 0;
enum {
	NP_Beat_Join = 1 << 0,
	NP_Beat_Create = 1 << 1,
};

typedef uint8_t NP_ResponseFlagsStorage;
static NP_ResponseFlagsStorage NP_ResponseFlags = 0;
enum {
	NP_Resp_Master = 1 << 0,
};

static void NP_CleanupPackets(NP_DataMessage** queue) {
	while (*queue != NULL) {
		NP_DataMessage* ptr = *queue;
		*queue = ptr->next;
		NutPunch_Free(ptr->data);
		NutPunch_Free(ptr);
	}
}

static void NP_NukeLobbyData() {
	NP_Closing = NP_Querying = 0;
	NP_LocalPeer = NUTPUNCH_MAX_PLAYERS;
	NP_ResponseFlags = 0;
	NutPunch_Memset(NP_LobbyMetadata, 0, sizeof(NP_LobbyMetadata));
	NutPunch_Memset(NP_PeerMetadata, 0, sizeof(NP_PeerMetadata));
	NutPunch_Memset(NP_MetadataOut, 0, sizeof(NP_MetadataOut));
	NutPunch_Memset(NP_Peers, 0, sizeof(NP_Peers));
	NutPunch_Memset(NP_Filters, 0, sizeof(NP_Filters));
	NP_CleanupPackets(&NP_QueueIn);
	NP_CleanupPackets(&NP_QueueOut);
}

static void NP_NukeRemote() {
	NP_LobbyId[0] = 0;
	NP_HeartbeatFlags = 0;
	NutPunch_Memset(&NP_PuncherPeer, 0, sizeof(NP_PuncherPeer));
	NutPunch_Memset(NP_Peers, 0, sizeof(NP_Peers));
	NutPunch_Memset(NP_PeerMetadata, 0, sizeof(NP_PeerMetadata));
	NutPunch_Memset(NP_Filters, 0, sizeof(NP_Filters));
	NP_LastStatus = NP_Status_Idle;
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
	NutPunch_Memset(&out->value, 0, sizeof(out->value));

	if (ipv == NP_IPv6) {
		((struct sockaddr_in6*)&out->value)->sin6_family = AF_INET6;
		((struct sockaddr_in6*)&out->value)->sin6_port = htons(port);
	} else {
		((struct sockaddr_in*)&out->value)->sin_family = AF_INET;
		((struct sockaddr_in*)&out->value)->sin_port = htons(port);
	}

	if (host == NULL)
		return 1;

	struct addrinfo *result = NULL, hints = {0};
	hints.ai_family = ipv == NP_IPv6 ? AF_INET6 : AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_flags = AI_PASSIVE;

	static char fmt[8] = {0};
	NutPunch_Memset(fmt, 0, sizeof(fmt));
	snprintf(fmt, sizeof(fmt), "%d", port);

	if (getaddrinfo(host, fmt, &hints, &result)) {
		NP_LastError = "Failed to get NutPuncher server address info";
		NP_LastErrorCode = NP_SockError();
		NP_PrintError();
		return 0;
	}
	if (result == NULL)
		return 0;

	NutPunch_Memset(&out->value, 0, sizeof(out->value));
	NutPunch_Memcpy(&out->value, result->ai_addr, result->ai_addrlen);
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
	NutPunch_Memset(buf, 0, sizeof(buf));

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
	return NP_GetMetadataFrom(NP_LobbyMetadata, name, size);
}

void* NutPunch_PeerGet(int peer, const char* name, int* size) {
	if (!NutPunch_PeerAlive(peer))
		goto none;
	if (peer < 0 || peer >= NUTPUNCH_MAX_PLAYERS)
		goto none;
	return NP_GetMetadataFrom(NP_PeerMetadata[peer], name, size);
none:
	if (size != NULL)
		*size = 0;
	return NULL;
}

static void NP_SetMetadataOut(int type, const char* name, int dataSize, const void* data) {
	if (!dataSize)
		return; // safe to skip e.g. 0-length strings entirely
	if (dataSize < 0) {
		NP_Log("Invalid metadata field size!");
		return;
	}

	int nameSize = NP_FieldNameSize(name);
	if (!nameSize)
		return;

	if (dataSize > NUTPUNCH_FIELD_DATA_MAX) {
		NP_Log("WARN: trimming metadata field from %d to %d bytes", dataSize, NUTPUNCH_FIELD_DATA_MAX);
		dataSize = NUTPUNCH_FIELD_DATA_MAX;
	}

	static const NutPunch_Field nullfield = {0};
	for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
		NutPunch_Field* ptr = &NP_MetadataOut[type][i];

		if (!NutPunch_Memcmp(ptr, &nullfield, sizeof(nullfield)))
			goto set;
		if (NP_FieldNameSize(ptr->name) == nameSize && !NutPunch_Memcmp(ptr->name, name, nameSize))
			goto set;
		continue;

	set:
		NutPunch_Memset(ptr->name, 0, sizeof(ptr->name));
		NutPunch_Memcpy(ptr->name, name, nameSize);

		NutPunch_Memset(ptr->data, 0, sizeof(ptr->data));
		NutPunch_Memcpy(ptr->data, data, dataSize);

		ptr->size = dataSize;
		return;
	}
}

void NutPunch_PeerSet(const char* name, int size, const void* data) {
	NP_SetMetadataOut(NP_MetaPeer, name, size, data);
}

void NutPunch_LobbySet(const char* name, int size, const void* data) {
	NP_SetMetadataOut(NP_MetaLobby, name, size, data);
}

static void NP_ExpectNutpuncher() {
	if (!NP_ServerHost[0]) {
		NutPunch_SetServerAddr(NUTPUNCH_DEFAULT_SERVER);
		NP_Log("Connecting to public NutPunch instance because no address was specified");
	}
}

static int NP_BindSocket(NP_IPv ipv) {
	NP_LazyInit();

	NP_Addr local = {0};
#ifdef NUTPUNCH_WINDOSE
	u_long argp = 1;
#endif
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

	if (
#ifdef NUTPUNCH_WINDOSE
		ioctlsocket(*sock, FIONBIO, &argp)
#else
		fcntl(*sock, F_SETFL, fcntl(*sock, F_GETFL, 0) | O_NONBLOCK)
#endif
		< 0)
	{
		NP_LastError = "Failed to set socket to non-blocking mode";
		goto fail;
	}

	NP_GetAddrInfo(&local, NULL, 0, ipv);
	if (!bind(*sock, (struct sockaddr*)&local.value, sizeof(local.value))) {
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
	NP_LastStatus = NP_Status_Error;
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

	NP_LastStatus = NP_Status_Online;
	if (lobbyId != NULL) {
		NutPunch_Memset(NP_LobbyId, 0, sizeof(NP_LobbyId));
		snprintf(NP_LobbyId, sizeof(NP_LobbyId), "%s", lobbyId);
	}
	return 1;

fail:
	NutPunch_Reset();
	NP_LastStatus = NP_Status_Error;
	return 0;
}

int NutPunch_Host(const char* lobbyId) {
	NP_HeartbeatFlags = NP_Beat_Join | NP_Beat_Create;
	return NutPunch_Connect(lobbyId);
}

int NutPunch_Join(const char* lobbyId) {
	NP_HeartbeatFlags = NP_Beat_Join;
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
	if (size > NUTPUNCH_BUFFER_SIZE - 512) {
		NP_Log("Ignoring a huge packet");
		return;
	}

	NP_DataMessage* next = NP_QueueOut;
	NP_QueueOut = (NP_DataMessage*)NutPunch_Malloc(sizeof(*next));
	NP_QueueOut->next = next;
	NP_QueueOut->peer = peer;
	NP_QueueOut->size = size;
	NP_QueueOut->data = (char*)NutPunch_Malloc(size);
	NP_QueueOut->bounce = index ? 0 : -1;
	NP_QueueOut->index = index;
	NP_QueueOut->dead = 0;
	NutPunch_Memcpy(NP_QueueOut->data, data, size);
}

NP_MakeHandler(NP_HandleIntro) {
	if (!NutPunch_Memcmp(&peer.value, &NP_PuncherPeer.value, sizeof(peer.value)))
		return;
	if (*data < NUTPUNCH_MAX_PLAYERS)
		NP_Peers[*data] = peer;
}

NP_MakeHandler(NP_HandleDisconnect) {
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		if (!NutPunch_Memcmp(&NP_Peers[i].value, &peer.value, sizeof(peer.value)))
			NP_KillPeer(i);
}

NP_MakeHandler(NP_HandleGTFO) {
	if (NutPunch_Memcmp(&peer.value, &NP_PuncherPeer.value, sizeof(peer.value)))
		return;

	NutPunch_Reset();
	NP_LastStatus = NP_Status_Error;
	NP_LastErrorCode = *data;

	switch (NP_LastErrorCode) {
	case NP_Err_NoSuchLobby:
		NP_LastError = "Lobby doesn't exist";
		break;
	case NP_Err_LobbyExists:
		NP_LastError = "Lobby already exists";
		break;
	default:
		break;
	}
}

NP_MakeHandler(NP_HandleJoin) {
	if (NutPunch_Memcmp(&peer.value, &NP_PuncherPeer.value, sizeof(peer.value)))
		return;

	NP_LocalPeer = NUTPUNCH_MAX_PLAYERS;
	const int metaSize = NUTPUNCH_MAX_FIELDS * sizeof(NutPunch_Field);

	NP_ResponseFlags = *data++;
	for (uint8_t i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		const uint8_t *ptr = data + i * (ptrdiff_t)(19 + metaSize), nulladdr[16] = {0};
		if (!NutPunch_Memcmp(ptr + 1, nulladdr, 16) && *(uint16_t*)(ptr + 17)) {
			NP_LocalPeer = i;
			break;
		}
	}
	if (NP_LocalPeer == NUTPUNCH_MAX_PLAYERS)
		return;

	static uint8_t hello[] = "INTR";
	hello[NUTPUNCH_HEADER_SIZE] = (uint8_t)NP_LocalPeer;

	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		peer.ipv = *data++;

		if (peer.ipv == NP_IPv6) {
			((struct sockaddr_in6*)&peer.value)->sin6_family = AF_INET6;
			NutPunch_Memcpy(&((struct sockaddr_in6*)&peer.value)->sin6_addr, data, 16);
		} else {
			((struct sockaddr_in*)&peer.value)->sin_family = AF_INET;
			NutPunch_Memcpy(&((struct sockaddr_in*)&peer.value)->sin_addr, data, 4);
		}
		data += 16;

		uint16_t* port = NP_AddrPort(peer);
		NutPunch_Memcpy(port, data, 2);
		data += 2;

		NutPunch_Memcpy(NP_PeerMetadata[i], data, metaSize);
		data += metaSize;

		if (i != NP_LocalPeer && *port) {
			const NP_Socket sock = peer.ipv == NP_IPv6 ? NP_Sock6 : NP_Sock4;
			sendto(sock, (char*)hello, sizeof(hello), 0, (struct sockaddr*)&peer.value, sizeof(peer.value));
		}
	}
	NutPunch_Memcpy(NP_LobbyMetadata, data, sizeof(NP_LobbyMetadata));
}

NP_MakeHandler(NP_HandleList) {
	if (NutPunch_Memcmp(&peer.value, &NP_PuncherPeer.value, sizeof(peer.value)))
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
		if (!NutPunch_Memcmp(&peer.value, &NP_Peers[i].value, sizeof(peer.value))) {
			peerIdx = i;
			break;
		}
	}
	if (peerIdx == NUTPUNCH_MAX_PLAYERS)
		return;

	const NP_PacketIdx netIndex = *(NP_PacketIdx*)data, index = ntohl(netIndex);
	size -= sizeof(index);
	data += sizeof(index);

	if (index) {
		static char ack[NUTPUNCH_HEADER_SIZE + sizeof(netIndex)] = "ACKY";
		NutPunch_Memcpy(ack + NUTPUNCH_HEADER_SIZE, &netIndex, sizeof(netIndex));
		NP_QueueSend(peerIdx, ack, sizeof(ack), 0);
	}

	NP_DataMessage* next = NP_QueueIn;
	NP_QueueIn = (NP_DataMessage*)NutPunch_Malloc(sizeof(*next));
	NP_QueueIn->data = (char*)NutPunch_Malloc(size);
	NutPunch_Memcpy(NP_QueueIn->data, data, size);
	NP_QueueIn->peer = peerIdx;
	NP_QueueIn->size = size;
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
	NP_Socket sock = NP_PuncherPeer.ipv == NP_IPv6 ? NP_Sock6 : NP_Sock4;
	if (sock == NUTPUNCH_INVALID_SOCKET)
		return 1;

	static char heartbeat[NUTPUNCH_HEARTBEAT_SIZE] = {0};
	NutPunch_Memset(heartbeat, 0, sizeof(heartbeat));

	char* ptr = heartbeat;
	if (NP_Querying) {
		NutPunch_Memcpy(ptr, "LIST", NUTPUNCH_HEADER_SIZE);
		ptr += NUTPUNCH_HEADER_SIZE;
		NutPunch_Memcpy(ptr, NP_Filters, sizeof(NP_Filters));
		ptr += sizeof(NP_Filters);
	} else {
		NutPunch_Memcpy(ptr, "JOIN", NUTPUNCH_HEADER_SIZE);
		ptr += NUTPUNCH_HEADER_SIZE;
		NutPunch_Memcpy(ptr, NP_LobbyId, NUTPUNCH_ID_MAX);
		ptr += NUTPUNCH_ID_MAX;
		// NOTE: make sure to correct endianness when multibyte flags become a thing.
		*(NP_HeartbeatFlagsStorage*)ptr = NP_HeartbeatFlags;
		ptr += sizeof(NP_HeartbeatFlags);
		NutPunch_Memcpy(ptr, NP_MetadataOut, sizeof(NP_MetadataOut));
		ptr += sizeof(NP_MetadataOut);
	}

	size_t length = ptr - heartbeat;
	int status = sendto(
		sock, heartbeat, (int)length, 0, (struct sockaddr*)&NP_PuncherPeer.value, sizeof(NP_PuncherPeer.value));
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

#ifdef NUTPUNCH_WINDOSE
	int
#else
	socklen_t
#endif
		addrSize;

	struct sockaddr_storage addr = {0};
	addrSize = sizeof(addr);
	NutPunch_Memset(&addr, 0, addrSize);

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
			if (!NutPunch_Memcmp(&addr, &NP_Peers[i].value, sizeof(addr))) {
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
			NP_Addr peer = {.value = addr, .ipv = ipv};
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
		NutPunch_Free(ptr->data);
		NutPunch_Free(ptr);
		goto findNext;
	}
}

static void NP_FlushOutQueue() {
	NP_PruneOutQueue();

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
		int result
			= sendto(sock, cur->data, (int)cur->size, 0, (struct sockaddr*)&peer.value, sizeof(peer.value));
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
	NP_FlushOutQueue();
	return NP_Status_Online;

sockFail:
	NP_LastErrorCode = NP_SockError();
	NP_NukeLobbyData();
	return NP_Status_Error;
}

int NutPunch_Update() {
	NP_LazyInit();
	if (NP_Sock4 == NUTPUNCH_INVALID_SOCKET && NP_Sock6 == NUTPUNCH_INVALID_SOCKET)
		return NP_Status_Idle;
	if (NP_LastStatus == NP_Status_Error)
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
	if (NP_LastStatus != NP_Status_Online)
		return 0;
	if (NutPunch_LocalPeer() == peer)
		return 1;
	return *NP_AddrPort(NP_Peers[peer]) != 0;
}

int NutPunch_LocalPeer() {
	return NP_LocalPeer;
}

int NutPunch_IsMaster() {
	return 0 != (NP_ResponseFlags & NP_Resp_Master);
}

#endif

#ifdef __cplusplus
}
#endif
