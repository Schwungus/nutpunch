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

/// The default NutPuncher instance. It's public, so feel free to use it.
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
	(NUTPUNCH_HEADER_SIZE + NUTPUNCH_MAX_PLAYERS * 6                                                               \
		+ (NUTPUNCH_MAX_PLAYERS + 1) * NUTPUNCH_MAX_FIELDS * (int)sizeof(struct NutPunch_Field))
#define NUTPUNCH_HEARTBEAT_SIZE                                                                                        \
	(NUTPUNCH_HEADER_SIZE + NUTPUNCH_ID_MAX                                                                        \
		+ NP_MOut_Size * NUTPUNCH_MAX_FIELDS * (int)sizeof(struct NutPunch_Field))

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum {
	NP_Status_Error,
	NP_Status_Idle,
	NP_Status_Online,
};

struct NutPunch_Field {
	char name[NUTPUNCH_FIELD_NAME_MAX], data[NUTPUNCH_FIELD_DATA_MAX];
	uint8_t size;
};

struct NutPunch_Filter {
	char name[NUTPUNCH_FIELD_NAME_MAX], value[NUTPUNCH_FIELD_DATA_MAX];
	int8_t comparison;
};

enum {
	NP_MOut_Peer,
	NP_MOut_Lobby,
	NP_MOut_Size,
};

/// Set a custom hole-puncher server address.
void NutPunch_SetServerAddr(const char* hostname);

/// Join a lobby by its ID. Query your connection status by calling `NutPunch_Update()` every frame.
bool NutPunch_Join(const char* lobbyId);

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

/// Check if there is a packet waiting in the receiving queue. Retrieve it with `NutPunch_NextPacket()`.
bool NutPunch_HasNext();

/// Retrieve the next packet in the receiving queue. Return the index of the peer who sent it.
int NutPunch_NextPacket(void* out, int* size);

/// Send data to specified peer. For reliable packet delivery, use `NutPunch_SendReliably`.
void NutPunch_Send(int peer, const void* data, int size);

/// Send data to specified peer expecting them to acknowledge the fact of reception.
void NutPunch_SendReliably(int peer, const void* data, int size);

/// Count how many "live" peers we have a route to, including our local peer.
///
/// Do not use this as an upper bound for iterating over peers. They can come in any order and with gaps in-between.
int NutPunch_PeerCount();

/// Return true if you are connected to the peer with the specified index.
///
/// If you're iterating over peers, use `NUTPUNCH_MAX_PLAYERS` as the upper index bound, and check their status using
/// this function.
bool NutPunch_PeerAlive(int peer);

/// Get the local peer's index. Returns `NUTPUNCH_MAX_PLAYERS` if this fails for any reason.
int NutPunch_LocalPeer();

/// Call this to gracefully disconnect from the lobby.
void NutPunch_Disconnect();

/// Query the lobbies given a set of filters. Make sure to call `NutPunch_SetServerAddr` beforehand.
///
/// For `comparison`, use `0` for an exact value match, `-1` for "less than the server's", and `1` vice versa.
///
/// The lobby list is queried every call to `NutPunch_Update`, so make sure you aren't skipping it.
void NutPunch_FindLobbies(int filterCount, const struct NutPunch_Filter* filters);

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
		fprintf(stdout, "<[NutPunch]> " __VA_ARGS__);                                                          \
		fprintf(stdout, "\n");                                                                                 \
		fflush(stdout);                                                                                        \
	} while (0)

#ifdef NUTPUNCH_IMPLEMENTATION

#ifdef NUTPUNCH_WINDOSE

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET NP_SocketType;
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

typedef int64_t NP_SocketType;
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

typedef uint64_t NP_PacketIdx;

struct NP_Packet {
	char* data;
	struct NP_Packet* next;
	NP_PacketIdx index;
	uint32_t size;
	uint8_t peer, dead;
	int16_t bounce;
};

struct NP_ServicePacket {
	const char identifier[NUTPUNCH_HEADER_SIZE];
	int packetSize;
	void (*const handler)(struct sockaddr, int, const uint8_t*);
};

#define NP_JOIN_LEN                                                                                                    \
	(NUTPUNCH_MAX_PLAYERS * 6                                                                                      \
		+ (NUTPUNCH_MAX_PLAYERS + 1) * NUTPUNCH_MAX_FIELDS * (int)sizeof(struct NutPunch_Field))
#define NP_LIST_LEN (NUTPUNCH_SEARCH_RESULTS_MAX * (NUTPUNCH_ID_MAX + 1))
#define NP_ACKY_LEN (sizeof(NP_PacketIdx))

#define NP_MakeHandler(name) static void name(struct sockaddr addr, int size, const uint8_t* data)
NP_MakeHandler(NP_HandleIntro);
NP_MakeHandler(NP_HandleDisconnect);
NP_MakeHandler(NP_HandleJoin);
NP_MakeHandler(NP_HandleList);
NP_MakeHandler(NP_HandleAcky);
NP_MakeHandler(NP_HandleData);

static const struct NP_ServicePacket NP_ServiceTable[] = {
	{'I', 'N', 'T', 'R', 1,           NP_HandleIntro     },
	{'D', 'I', 'S', 'C', 0,           NP_HandleDisconnect},
	{'J', 'O', 'I', 'N', NP_JOIN_LEN, NP_HandleJoin      },
	{'L', 'I', 'S', 'T', NP_LIST_LEN, NP_HandleList      },
	{'A', 'C', 'K', 'Y', NP_ACKY_LEN, NP_HandleAcky      },
	{'D', 'A', 'T', 'A', -1,          NP_HandleData      },
};

static const char* NP_LastError = NULL;
static int NP_LastErrorCode = 0;

static bool NP_InitDone = false, NP_Closing = false;
static int NP_LastStatus = NP_Status_Idle;

static char NP_LobbyId[NUTPUNCH_ID_MAX + 1] = {0};
static struct sockaddr NP_Peers[NUTPUNCH_MAX_PLAYERS] = {0};
static uint8_t NP_LocalPeer = NUTPUNCH_MAX_PLAYERS;

static NP_SocketType NP_Socket = NUTPUNCH_INVALID_SOCKET;
static struct sockaddr NP_RemoteAddr = {0};
static char NP_ServerHost[128] = {0};

static struct NP_Packet *NP_QueueIn = NULL, *NP_QueueOut = NULL;
static struct NutPunch_Field NP_LobbyMetadata[NUTPUNCH_MAX_FIELDS] = {0},
			     NP_PeerMetadata[NUTPUNCH_MAX_PLAYERS][NUTPUNCH_MAX_FIELDS] = {0},
			     NP_MetadataOut[NP_MOut_Size][NUTPUNCH_MAX_FIELDS] = {0};

static bool NP_Querying = false;
static struct NutPunch_Filter NP_Filters[NUTPUNCH_SEARCH_FILTERS_MAX] = {0};

static char NP_LobbyNames[NUTPUNCH_SEARCH_RESULTS_MAX][NUTPUNCH_ID_MAX + 1] = {0};
static char* NP_Lobbies[NUTPUNCH_SEARCH_RESULTS_MAX] = {0};

static void NP_CleanupPackets(struct NP_Packet** queue) {
	while (*queue != NULL) {
		struct NP_Packet* ptr = *queue;
		*queue = ptr->next;
		NutPunch_Free(ptr->data);
		NutPunch_Free(ptr);
	}
}

static void NP_NukeLobbyData() {
	NP_Closing = NP_Querying = false;
	NP_LocalPeer = NUTPUNCH_MAX_PLAYERS;
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
	NutPunch_Memset(&NP_RemoteAddr, 0, sizeof(NP_RemoteAddr));
	NutPunch_Memset(NP_ServerHost, 0, sizeof(NP_ServerHost));
	NutPunch_Memset(NP_Peers, 0, sizeof(NP_Peers));
	NutPunch_Memset(NP_PeerMetadata, 0, sizeof(NP_PeerMetadata));
	NutPunch_Memset(NP_Filters, 0, sizeof(NP_Filters));
}

static void NP_NukeSocket() {
	NP_NukeLobbyData();
	if (NP_Socket != NUTPUNCH_INVALID_SOCKET) {
#ifdef NUTPUNCH_WINDOSE
		closesocket(NP_Socket);
#else
		close(NP_Socket);
#endif
		NP_Socket = NUTPUNCH_INVALID_SOCKET;
	}
}

static void NP_LazyInit() {
	if (NP_InitDone)
		return;
	NP_InitDone = true;

#ifdef NUTPUNCH_WINDOSE
	WSADATA bitch = {0};
	WSAStartup(MAKEWORD(2, 2), &bitch);
#endif

	for (int i = 0; i < NUTPUNCH_SEARCH_RESULTS_MAX; i++)
		NP_Lobbies[i] = NP_LobbyNames[i];
	NP_NukeSocket();
}

static void NP_PrintError() {
	if (NP_LastErrorCode)
		NP_Log("WARN: %s (error code: %d)", NP_LastError, NP_LastErrorCode);
	else
		NP_Log("WARN: %s", NP_LastError);
}

static struct sockaddr NP_SockAddr(const char* host, uint16_t port) {
	NP_LazyInit();

	struct addrinfo *result = NULL, hints = {0};
	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (host == NULL)
		goto skip;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_flags = AI_PASSIVE;

	static char fmt[32] = {0};
	snprintf(fmt, sizeof(fmt), "%d", port);
	if (getaddrinfo(host, fmt, &hints, &result) != 0) {
		NP_LastError = "Failed to get NutPuncher server address info";
		NP_LastErrorCode = NP_SockError();
		NP_PrintError();
		goto skip;
	}
	if (result == NULL)
		goto skip;

	NutPunch_Memcpy(&addr, result->ai_addr, sizeof(addr));
	freeaddrinfo(result);

skip:
	return *(struct sockaddr*)&addr;
}

void NutPunch_SetServerAddr(const char* hostname) {
	NP_LazyInit();
	if (hostname == NULL)
		NP_ServerHost[0] = 0;
	else
		snprintf(NP_ServerHost, sizeof(NP_ServerHost), "%s", hostname);
}

void NutPunch_Reset() {
	NP_NukeRemote();
	NP_NukeSocket();
}

static int NP_FieldNameSize(const char* name) {
	for (int i = 0; i < NUTPUNCH_FIELD_NAME_MAX; i++)
		if (!name[i])
			return i;
	return NUTPUNCH_FIELD_NAME_MAX;
}

static void* NP_GetMetadataFrom(struct NutPunch_Field fields[NUTPUNCH_MAX_FIELDS], const char* name, int* size) {
	static char buf[NUTPUNCH_FIELD_DATA_MAX] = {0};
	NutPunch_Memset(buf, 0, sizeof(buf));

	int nameSize = NP_FieldNameSize(name);
	if (!nameSize)
		goto none;

	for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
		struct NutPunch_Field* ptr = &fields[i];
		if (!NutPunch_Memcmp(ptr->name, name, nameSize)) {
			NutPunch_Memcpy(buf, ptr->data, ptr->size);
			if (size != NULL)
				*size = ptr->size;
			return buf;
		}
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

	static const struct NutPunch_Field nullfield = {0};
	for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
		struct NutPunch_Field* ptr = &NP_MetadataOut[type][i];

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
	NP_SetMetadataOut(NP_MOut_Peer, name, size, data);
}

void NutPunch_LobbySet(const char* name, int size, const void* data) {
	NP_SetMetadataOut(NP_MOut_Lobby, name, size, data);
}

static bool NP_BindSocket() {
	struct sockaddr addr;
#ifdef NUTPUNCH_WINDOSE
	u_long argp = 1;
#endif

	NP_NukeSocket();

	NP_Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (NP_Socket == NUTPUNCH_INVALID_SOCKET) {
		NP_LastError = "Failed to create the underlying UDP socket";
		goto fail;
	}

	if (
#ifdef NUTPUNCH_WINDOSE
		ioctlsocket(NP_Socket, FIONBIO, &argp)
#else
		fcntl(NP_Socket, F_SETFL, fcntl(NP_Socket, F_GETFL, 0) | O_NONBLOCK)
#endif
		< 0)
	{
		NP_LastError = "Failed to set socket to non-blocking mode";
		goto fail;
	}

	addr = NP_SockAddr(NULL, 0);
	if (bind(NP_Socket, &addr, sizeof(addr)) < 0) {
		NP_LastError = "Failed to bind the UDP socket";
		goto fail;
	}

	NP_LastStatus = NP_Status_Online;
	if (!NP_ServerHost[0])
		NutPunch_SetServerAddr(NUTPUNCH_DEFAULT_SERVER);
	NP_RemoteAddr = NP_SockAddr(NP_ServerHost, NUTPUNCH_SERVER_PORT);
	return true;

fail:
	NP_LastStatus = NP_Status_Error;
	NP_LastErrorCode = NP_SockError();
	NP_PrintError();
	NutPunch_Reset();
	return false;
}

static void NP_ExpectNutpuncher() {
	if (!NP_ServerHost[0]) {
		NutPunch_SetServerAddr(NUTPUNCH_DEFAULT_SERVER);
		NP_Log("Connecting to NutPunch public instance because no address was specified");
	}
}

bool NutPunch_Join(const char* lobbyId) {
	NP_LazyInit();
	NP_ExpectNutpuncher();

	if (NP_BindSocket()) {
		NP_LastStatus = NP_Status_Online;
		NutPunch_Memset(NP_LobbyId, 0, sizeof(NP_LobbyId));
		snprintf(NP_LobbyId, sizeof(NP_LobbyId), "%s", lobbyId);
		return true;
	}

	NutPunch_Reset();
	NP_LastStatus = NP_Status_Error;
	return false;
}

void NutPunch_Disconnect() {
	NP_Closing = true;
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

static void NP_SendEx(int peer, const void* data, int size, NP_PacketIdx index) {
	if (!NutPunch_PeerAlive(peer) || NutPunch_LocalPeer() == peer)
		return;
	if (size > NUTPUNCH_BUFFER_SIZE - 512) {
		NP_Log("Ignoring a huge packet");
		return;
	}

	struct NP_Packet* next = NP_QueueOut;
	NP_QueueOut = (struct NP_Packet*)NutPunch_Malloc(sizeof(*next));
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
	if (!NutPunch_Memcmp(&addr, &NP_RemoteAddr, sizeof(addr)))
		return;
	if (*data < NUTPUNCH_MAX_PLAYERS)
		NP_Peers[*data] = addr;
}

NP_MakeHandler(NP_HandleDisconnect) {
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		if (!NutPunch_Memcmp(&NP_Peers[i], &addr, sizeof(addr))) {
			NutPunch_Memset(&NP_Peers[i], 0, sizeof(addr));
			return;
		}
}

NP_MakeHandler(NP_HandleJoin) {
	if (NutPunch_Memcmp(&addr, &NP_RemoteAddr, sizeof(addr)))
		return;

	NP_LocalPeer = NUTPUNCH_MAX_PLAYERS;
	const int metaSize = NUTPUNCH_MAX_FIELDS * sizeof(struct NutPunch_Field);

	for (uint8_t i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		const uint8_t* ptr = data + i * (ptrdiff_t)(6 + metaSize);
		if (!*(uint32_t*)ptr && *(uint16_t*)(ptr + 4)) {
			NP_LocalPeer = i;
			break;
		}
	}
	if (NP_LocalPeer == NUTPUNCH_MAX_PLAYERS)
		return;

	static char hello[] = {'I', 'N', 'T', 'R', 0};
	hello[NUTPUNCH_HEADER_SIZE] = NP_LocalPeer;

	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		((struct sockaddr_in*)&addr)->sin_family = AF_INET;
		NutPunch_Memcpy(&((struct sockaddr_in*)&addr)->sin_addr, data, 4);
		data += 4;
		NutPunch_Memcpy(&((struct sockaddr_in*)&addr)->sin_port, data, 2);
		data += 2;
		NutPunch_Memcpy(NP_PeerMetadata[i], data, metaSize);
		data += metaSize;
		if (i != NP_LocalPeer && ((struct sockaddr_in*)&addr)->sin_port)
			sendto(NP_Socket, hello, sizeof(hello), 0, &addr, sizeof(addr));
	}
	NutPunch_Memcpy(NP_LobbyMetadata, data, sizeof(NP_LobbyMetadata));
}

NP_MakeHandler(NP_HandleList) {
	if (NutPunch_Memcmp(&addr, &NP_RemoteAddr, sizeof(addr)))
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
		if (!NutPunch_Memcmp(&addr, NP_Peers + i, sizeof(addr))) {
			peerIdx = i;
			break;
		}
	}
	if (peerIdx == NUTPUNCH_MAX_PLAYERS)
		return;

	NP_PacketIdx index = *(NP_PacketIdx*)data;
	size -= sizeof(index);
	data += sizeof(index);

	if (index) {
		static char ack[NUTPUNCH_HEADER_SIZE + sizeof(index)] = "ACKY";
		NutPunch_Memcpy(ack + NUTPUNCH_HEADER_SIZE, &index, sizeof(index));
		NP_SendEx(peerIdx, ack, sizeof(ack), 0);
	}

	struct NP_Packet* next = NP_QueueIn;
	NP_QueueIn = (struct NP_Packet*)NutPunch_Malloc(sizeof(*next));
	NP_QueueIn->data = (char*)NutPunch_Malloc(size);
	NutPunch_Memcpy(NP_QueueIn->data, data, size);
	NP_QueueIn->peer = peerIdx;
	NP_QueueIn->size = size;
	NP_QueueIn->next = next;
}

NP_MakeHandler(NP_HandleAcky) {
	NP_PacketIdx index = *(NP_PacketIdx*)data;
	for (struct NP_Packet* ptr = NP_QueueOut; ptr != NULL; ptr = ptr->next)
		if (ptr->index == index) {
			ptr->dead = true;
			return;
		}
}

static bool NP_SendHeartbeat() {
	static char heartbeat[NUTPUNCH_HEARTBEAT_SIZE] = {0};
	NutPunch_Memset(heartbeat, 0, sizeof(heartbeat));

	char* beat = heartbeat;
	int length = NUTPUNCH_HEARTBEAT_SIZE;

	if (NP_Querying) {
		NutPunch_Memcpy(beat, "LIST", NUTPUNCH_HEADER_SIZE);
		beat += NUTPUNCH_HEADER_SIZE;
		NutPunch_Memcpy(beat, NP_Filters, sizeof(NP_Filters));
		length = NUTPUNCH_HEADER_SIZE + sizeof(NP_Filters);
		goto send;
	}

	NutPunch_Memcpy(beat, "JOIN", NUTPUNCH_HEADER_SIZE);
	beat += NUTPUNCH_HEADER_SIZE;
	NutPunch_Memcpy(beat, NP_LobbyId, NUTPUNCH_ID_MAX);
	beat += NUTPUNCH_ID_MAX;

	for (int i = 0; i < NP_MOut_Size; i++) {
		NutPunch_Memcpy(beat, NP_MetadataOut[i], sizeof(NP_MetadataOut[i]));
		beat += sizeof(NP_MetadataOut[i]);
	}

send:
	if (sendto(NP_Socket, heartbeat, length, 0, &NP_RemoteAddr, sizeof(NP_RemoteAddr)) < 0
		&& NP_SockError() != NP_WouldBlock && NP_SockError() != NP_ConnReset)
	{
		NP_LastError = "Failed to send heartbeat to NutPuncher";
		return false;
	}

	return true;
}

static int NP_ReceiveShit() {
#ifdef NUTPUNCH_WINDOSE
	int
#else
	socklen_t
#endif
		addrSize;

	struct sockaddr addr = {0};
	addrSize = sizeof(addr);
	NutPunch_Memset(&addr, 0, addrSize);

	static char buf[NUTPUNCH_BUFFER_SIZE] = {0};
	int size = recvfrom(NP_Socket, buf, sizeof(buf), 0, &addr, &addrSize);
	if (size < 0) {
		if (NP_SockError() == NP_WouldBlock)
			return 1;
		if (NP_SockError() == NP_ConnReset)
			return 0;
		NP_LastError = "Failed to receive from holepunch server";
		return -1;
	}

	if (!size) // graceful disconnection
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			if (!NutPunch_Memcmp(&addr, NP_Peers + i, addrSize)) {
				NP_KillPeer(i);
				return 0;
			}

	if (size < NUTPUNCH_HEADER_SIZE)
		return 0;
	size -= NUTPUNCH_HEADER_SIZE;

	for (int i = 0; i < sizeof(NP_ServiceTable) / sizeof(*NP_ServiceTable); i++) {
		const struct NP_ServicePacket entry = NP_ServiceTable[i];

		if (!NutPunch_Memcmp(buf, entry.identifier, NUTPUNCH_HEADER_SIZE)
			&& (entry.packetSize < 0 || size == entry.packetSize))
		{
			entry.handler(addr, size, (uint8_t*)(buf + NUTPUNCH_HEADER_SIZE));
			return 0;
		}
	}

	return 0;
}

static void NP_PruneOutQueue() {
findNext:
	for (struct NP_Packet* ptr = NP_QueueOut; ptr != NULL; ptr = ptr->next) {
		if (!ptr->dead)
			continue;

		for (struct NP_Packet* other = NP_QueueOut; other != NULL; other = other->next)
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

	for (struct NP_Packet* cur = NP_QueueOut; cur != NULL; cur = cur->next) {
		struct sockaddr addr = NP_Peers[cur->peer];
		if (!NutPunch_PeerAlive(cur->peer)) {
			cur->dead = true;
			continue;
		}

		if (cur->bounce < 0) {
			cur->dead = true;
			goto send;
		}

		if (cur->bounce > 0)
			cur->bounce--;
		if (cur->bounce > 0)
			continue;
		else
			cur->bounce = NUTPUNCH_BOUNCE_TICKS;

	send:
		int result = sendto(NP_Socket, cur->data, cur->size, 0, &addr, sizeof(addr));
		if (result < 0 && NP_SockError() != NP_WouldBlock) {
			if (NP_SockError() == NP_ConnReset)
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
}

static int NP_RealUpdate() {
	if (NP_Socket == NUTPUNCH_INVALID_SOCKET)
		return NP_Status_Idle;

	if (!NP_SendHeartbeat())
		goto sockFail;

	for (;;)
		switch (NP_ReceiveShit()) {
			case 1:
				goto recvDone;
			case -1:
				goto sockFail;
			default:
				break;
		}

recvDone:
	static char bye[4] = {'D', 'I', 'S', 'C'};
	if (!NP_Closing)
		goto flush;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		for (int kkk = 0; kkk < 10; kkk++)
			NP_SendEx(i, bye, sizeof(bye), 0);

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

	if (NP_LastStatus != NP_Status_Idle)
		NP_LastStatus = NP_RealUpdate();
	if (NP_LastStatus == NP_Status_Error) {
		NP_LastStatus = NP_Status_Idle;
		NP_PrintError();
		NP_NukeRemote();
		NP_NukeSocket();
	}

	return NP_LastStatus;
}

bool NutPunch_HasNext() {
	return NP_QueueIn != NULL;
}

int NutPunch_NextPacket(void* out, int* size) {
	if (*size < NP_QueueIn->size) {
		NP_Log("WARN: not enough memory allocated to copy the next packet");
		return NUTPUNCH_MAX_PLAYERS;
	}

	if (size != NULL)
		*size = NP_QueueIn->size;
	NutPunch_Memcpy(out, NP_QueueIn->data, NP_QueueIn->size);
	NutPunch_Free(NP_QueueIn->data);

	int sourcePeer = NP_QueueIn->peer;
	struct NP_Packet* next = NP_QueueIn->next;
	NutPunch_Free(NP_QueueIn);

	NP_QueueIn = next;
	return sourcePeer;
}

static void NP_SendData(int peer, const void* data, int dataSize, bool reliable) {
	static char buf[NUTPUNCH_BUFFER_SIZE] = "DATA";
	static NP_PacketIdx packetIdx = 1;

	if (dataSize > NUTPUNCH_BUFFER_SIZE - 32) {
		NP_Log("Ignoring a huge packet");
		return;
	}

	const NP_PacketIdx index = reliable ? packetIdx : 0;
	if (reliable)
		packetIdx++;

	char* ptr = buf + NUTPUNCH_HEADER_SIZE;
	NutPunch_Memcpy(ptr, &index, sizeof(index));
	ptr += sizeof(index);

	NutPunch_Memcpy(ptr, data, dataSize);
	NP_SendEx(peer, buf, NUTPUNCH_HEADER_SIZE + sizeof(index) + dataSize, index);
}

void NutPunch_Send(int peer, const void* data, int size) {
	NP_SendData(peer, data, size, false);
}

void NutPunch_SendReliably(int peer, const void* data, int size) {
	NP_SendData(peer, data, size, true);
}

void NutPunch_FindLobbies(int filterCount, const struct NutPunch_Filter* filters) {
	NP_LazyInit();
	NP_ExpectNutpuncher();

	if (filterCount < 1)
		return;
	else if (filterCount > NUTPUNCH_SEARCH_FILTERS_MAX)
		filterCount = NUTPUNCH_SEARCH_FILTERS_MAX;

	if (!NP_BindSocket())
		goto fail;
	NP_Querying = true;
	NutPunch_Memcpy(NP_Filters, filters, filterCount * sizeof(*filters));
	return;

fail:
	NP_PrintError();
	NutPunch_Reset();
	NP_LastStatus = NP_Status_Error;
	return;
}

const char* NutPunch_GetLobby(int index) {
	NP_LazyInit();
	return index < NutPunch_LobbyCount() ? NP_Lobbies[index] : NULL;
}

int NutPunch_LobbyCount() {
	NP_LazyInit();
	int count = 0;

	static const char nully[NUTPUNCH_ID_MAX + 1] = {0};
	while (count < NUTPUNCH_SEARCH_RESULTS_MAX) {
		if (!NutPunch_Memcmp(NP_Lobbies[count], nully, sizeof(nully)))
			break;
		count += 1;
	}

	return count;
}

int NutPunch_PeerCount() {
	int count = 0;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		count += NutPunch_PeerAlive(i);
	return count;
}

const void* NutPunch_ServerAddr() {
	return &NP_RemoteAddr;
}

bool NutPunch_PeerAlive(int peer) {
	if (NutPunch_LocalPeer() == peer)
		return true;
	if (NP_LastStatus != NP_Status_Online)
		return false;
	return 0 != ((struct sockaddr_in*)(NP_Peers + peer))->sin_port;
}

int NutPunch_LocalPeer() {
	return NP_LocalPeer;
}

#endif

#ifdef __cplusplus
}
#endif
