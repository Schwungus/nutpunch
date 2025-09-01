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

#define NUTPUNCH_FIELD_NAME_MAX (8)
#define NUTPUNCH_FIELD_DATA_MAX (32)
#define NUTPUNCH_MAX_FIELDS (16)

#define NUTPUNCH_MAX(a, b) ((a) > (b) ? (a) : (b))
#define NUTPUNCH_HEADER_SIZE (4)

#define NUTPUNCH_RESPONSE_SIZE                                                                                         \
	(NUTPUNCH_HEADER_SIZE                                                                                          \
		+ NUTPUNCH_MAX(NUTPUNCH_MAX_PLAYERS * (int)sizeof(struct NutPunch)                                     \
				       + NUTPUNCH_MAX_FIELDS * (int)sizeof(struct NutPunch_Field),                     \
			NUTPUNCH_SEARCH_RESULTS_MAX * NUTPUNCH_ID_MAX))
#define NUTPUNCH_HEARTBEAT_SIZE                                                                                        \
	(NUTPUNCH_HEADER_SIZE                                                                                          \
		+ NUTPUNCH_MAX(NUTPUNCH_ID_MAX + NUTPUNCH_MAX_FIELDS * (int)sizeof(struct NutPunch_Field),             \
			NUTPUNCH_SEARCH_FILTERS_MAX * (int)sizeof(struct NutPunch_Filter)))

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum {
	NP_Status_Error,
	NP_Status_Idle,
	NP_Status_Online,
};

struct NutPunch {
	uint8_t addr[4];
	uint16_t port;
};

struct NutPunch_Field {
	char name[NUTPUNCH_FIELD_NAME_MAX], data[NUTPUNCH_FIELD_DATA_MAX];
	uint8_t size;
};

struct NutPunch_Filter {
	char name[NUTPUNCH_FIELD_NAME_MAX], value[NUTPUNCH_FIELD_DATA_MAX];
	int8_t comparison;
};

// Forward-declarations:

/// Set a custom hole-puncher server address.
void NutPunch_SetServerAddr(const char* hostname);

/// Initiate a nut-punch to the specified lobby. Query status by calling `NutPunch_Update()` every frame.
bool NutPunch_Join(const char* lobbyId);

/// Run this at the end of your program to do semi-important cleanup.
void NutPunch_Cleanup();

/// Call this every frame to update nutpunch. Returns one of the `NP_Status_*` constants.
int NutPunch_Update();

/// Request lobby metadata to be set. Can be called multiple times in a row. Will send out metadata changes on
/// `NutPunch_Update()`, and won't do anything unless you're the lobby's master.
///
/// See `NUTPUNCH_FIELD_NAME_MAX` and `NUTPUNCH_FIELD_DATA_MAX` for limitations on the amount of data you can squeeze.
void NutPunch_Set(const char* key, int size, const void* data);

/// Request lobby metadata. Sets `size` to the field's actual size if the pointer isn't `NULL`.
///
/// The resulting pointer is actually a static allocation, so don't rely too much on it; its data will change after the
/// next `NutPunch_Get` call.
void* NutPunch_Get(const char* key, int* size);

/// Return true if there is a packet in the receiving queue. Retrieve it with `NutPunch_NextPacket()`.
bool NutPunch_HasNext();

/// Retrieve the next packet in the receiving queue. Return the index of the peer who sent it.
int NutPunch_NextPacket(void* out, int* size);

/// Send data to specified peer.
void NutPunch_Send(int peer, const void* data, int size);

/// Count how many "live" peers we're expected to have, including our local peer.
int NutPunch_PeerCount();

/// Return true if you are connected to peer with the specified index.
bool NutPunch_PeerAlive(int peer);

/// Get the local peer's index. Returns `NUTPUNCH_MAX_PLAYERS` if this fails for any reason.
int NutPunch_LocalPeer();

/// Call this to gracefully disconnect from the lobby.
void NutPunch_Disconnect();

/// Query the lobbies given a set of filters. Make sure to call `NutPunch_SetServerAddr` beforehand.
///
/// For `comparison`, use `0` for an exact value match, `-1` for "less than the server's", and `1` vice versa.
void NutPunch_FindLobbies(int filterCount, const struct NutPunch_Filter* filters);

/// Reap the fruits of `NutPunch_FindLobbies`. Updates every call to `NutPunch_Update`.
const char** NutPunch_LobbyList(int* length);

/// Use this to reset the underlying socket in case of an inexplicable error.
void NutPunch_Reset();

/// Get the human-readable description of the latest error in `NutPunch_Update()`.
const char* NutPunch_GetLastError();

#define NP_Log(...)                                                                                                    \
	do {                                                                                                           \
		fprintf(stdout, __VA_ARGS__);                                                                          \
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

struct NP_Packet {
	char* data;
	int size, peer;
	struct NP_Packet* next;
};

struct NP_ServicePacket {
	const char identifier[NUTPUNCH_HEADER_SIZE];
	void (*const handler)(struct sockaddr, int, const uint8_t*);
};

#define NP_MakeHandler(name) static void name(struct sockaddr addr, int size, const uint8_t* data)
NP_MakeHandler(NP_HandleIntro);
NP_MakeHandler(NP_HandleDisconnect);
NP_MakeHandler(NP_HandleJoin);
NP_MakeHandler(NP_HandleList);

static const struct NP_ServicePacket NP_ServiceTable[] = {
	{'I', 'N', 'T', 'R', NP_HandleIntro},
	{'D', 'I', 'S', 'C', NP_HandleDisconnect},
	{'J', 'O', 'I', 'N', NP_HandleJoin},
	{'L', 'I', 'S', 'T', NP_HandleList},
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
static struct NutPunch_Field NP_MetadataIn[NUTPUNCH_MAX_FIELDS] = {0}, NP_MetadataOut[NUTPUNCH_MAX_FIELDS] = {0};

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
	NutPunch_Memset(NP_MetadataIn, 0, sizeof(NP_MetadataIn));
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
	NP_Socket = NUTPUNCH_INVALID_SOCKET;

	for (int i = 0; i < NUTPUNCH_SEARCH_RESULTS_MAX; i++)
		NP_Lobbies[i] = NP_LobbyNames[i];
	NP_NukeLobbyData();
}

static void NP_PrintError() {
	if (NP_LastErrorCode)
		NP_Log("WARN: %s (error code: %d)", NP_LastError, NP_LastErrorCode);
	else
		NP_Log("WARN: %s", NP_LastError);
}

static struct sockaddr NP_SockAddr(const char* host, uint16_t port) {
	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (host != NULL) {
		uint32_t conv = inet_addr(host);
		NutPunch_Memcpy(&addr.sin_addr, &conv, 4);
	}
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

void* NutPunch_Get(const char* name, int* size) {
	static char copy[NUTPUNCH_FIELD_DATA_MAX] = {0};

	int nameSize = NP_FieldNameSize(name);
	if (!nameSize)
		goto skip;

	for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
		struct NutPunch_Field* ptr = &NP_MetadataIn[i];
		if (!NutPunch_Memcmp(ptr->name, name, nameSize)) {
			NutPunch_Memset(copy, 0, sizeof(copy));
			*size = ptr->size;
			NutPunch_Memcpy(copy, ptr->data, *size);
			return copy;
		}
	}

skip:
	NutPunch_Memset(copy, 0, sizeof(copy));
	*size = 0;
	return copy;
}

void NutPunch_Set(const char* name, int dataSize, const void* data) {
	int nameSize = NP_FieldNameSize(name);
	if (!nameSize)
		return;
	if (nameSize > NUTPUNCH_FIELD_NAME_MAX)
		nameSize = NUTPUNCH_FIELD_NAME_MAX;

	if (dataSize <= 0) {
		NP_Log("Invalid metadata field size!");
		return;
	}
	if (dataSize > NUTPUNCH_FIELD_DATA_MAX) {
		NP_Log("WARN: trimming metadata field from %d to %d bytes", dataSize, NUTPUNCH_FIELD_DATA_MAX);
		dataSize = NUTPUNCH_FIELD_DATA_MAX;
	}

	static const struct NutPunch_Field nullfield = {0};
	for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
		struct NutPunch_Field* ptr = &NP_MetadataOut[i];
		if (!NutPunch_Memcmp(ptr->name, name, nameSize) || !NutPunch_Memcmp(ptr, &nullfield, sizeof(nullfield)))
		{
			NutPunch_Memset(ptr->name, 0, sizeof(ptr->name));
			NutPunch_Memcpy(ptr->name, name, nameSize);

			NutPunch_Memset(ptr->data, 0, sizeof(ptr->data));
			NutPunch_Memcpy(ptr->data, data, dataSize);

			ptr->size = dataSize;
			return;
		}
	}
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
	NP_RemoteAddr = NP_SockAddr(NP_ServerHost, NUTPUNCH_SERVER_PORT);
	return true;

fail:
	NP_LastStatus = NP_Status_Error;
	NP_LastErrorCode = NP_SockError();
	NP_PrintError();
	NutPunch_Reset();
	return false;
}

bool NutPunch_Join(const char* lobbyId) {
	NP_LazyInit();

	if (!NP_ServerHost[0]) {
		NP_LastError = "Holepuncher server address unset";
		NP_LastErrorCode = 0;
		NP_PrintError();
		goto fail;
	}

	if (NP_BindSocket()) {
		NP_LastStatus = NP_Status_Online;
		NutPunch_Memset(NP_LobbyId, 0, sizeof(NP_LobbyId));
		snprintf(NP_LobbyId, sizeof(NP_LobbyId), "%s", lobbyId);
		return true;
	}

fail:
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

NP_MakeHandler(NP_HandleIntro) {
	if (!NutPunch_Memcmp(&addr, &NP_RemoteAddr, sizeof(addr)))
		return;
	if (size == 1)
		NP_Peers[*data] = addr;
}

NP_MakeHandler(NP_HandleDisconnect) {
	if (size)
		return;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		if (!NutPunch_Memcmp(&NP_Peers[i], &addr, sizeof(addr))) {
			NutPunch_Memset(&NP_Peers[i], 0, sizeof(addr));
			return;
		}
}

NP_MakeHandler(NP_HandleJoin) {
	if (NutPunch_Memcmp(&addr, &NP_RemoteAddr, sizeof(addr)))
		return;
	if (size != NUTPUNCH_RESPONSE_SIZE - NUTPUNCH_HEADER_SIZE)
		return;

	NP_LocalPeer = NUTPUNCH_MAX_PLAYERS;
	for (uint8_t i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		const uint8_t* ptr = data + ((size_t)i * 6);
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
		if (i != NP_LocalPeer && ((struct sockaddr_in*)&addr)->sin_port)
			sendto(NP_Socket, hello, sizeof(hello), 0, &addr, sizeof(addr));
	}
	NutPunch_Memcpy(NP_MetadataIn, data, sizeof(NP_MetadataIn));
}

NP_MakeHandler(NP_HandleList) {
	if (NutPunch_Memcmp(&addr, &NP_RemoteAddr, sizeof(addr)))
		return;
	if (size != NUTPUNCH_RESPONSE_SIZE - NUTPUNCH_HEADER_SIZE)
		return;
	for (int i = 0; i < NUTPUNCH_SEARCH_RESULTS_MAX; i++) {
		NutPunch_Memcpy(NP_Lobbies[i], data, NUTPUNCH_ID_MAX);
		NP_Lobbies[i][NUTPUNCH_ID_MAX] = '\0';
		data += NUTPUNCH_ID_MAX;
	}
}

static int NP_RealUpdate() {
	if (NP_Socket == NUTPUNCH_INVALID_SOCKET)
		return NP_Status_Idle;

	static char heartbeat[NUTPUNCH_HEARTBEAT_SIZE] = {0};
#ifdef NUTPUNCH_WINDOSE
	int
#else
	socklen_t
#endif
		addrSize;
	struct sockaddr addr = NP_RemoteAddr;
	addrSize = sizeof(addr);

	char* beat = heartbeat;
	if (NP_Querying) {
		NutPunch_Memcpy(beat, "LIST", NUTPUNCH_HEADER_SIZE);
		beat += NUTPUNCH_HEADER_SIZE;
		NutPunch_Memcpy(beat, NP_Filters, sizeof(NP_Filters));
	} else {
		NutPunch_Memcpy(beat, "JOIN", NUTPUNCH_HEADER_SIZE);
		beat += NUTPUNCH_HEADER_SIZE;
		NutPunch_Memcpy(beat, NP_LobbyId, NUTPUNCH_ID_MAX);
		beat += NUTPUNCH_ID_MAX;
		NutPunch_Memcpy(beat, NP_MetadataOut, sizeof(NP_MetadataOut));
	}

	if (sendto(NP_Socket, heartbeat, sizeof(heartbeat), 0, &addr, sizeof(addr)) < 0
		&& NP_SockError() != NP_WouldBlock && NP_SockError() != NP_ConnReset)
	{
		NP_LastError = "Failed to send heartbeat to NutPuncher";
		goto sockFail;
	}

	for (;;) {
		int connIdx = NUTPUNCH_MAX_PLAYERS;
		struct NP_Packet* next = NP_QueueIn;
		addrSize = sizeof(addr);
		NutPunch_Memset(&addr, 0, addrSize);

		static char buf[NUTPUNCH_BUFFER_SIZE] = {0};
		int size = recvfrom(NP_Socket, buf, sizeof(buf), 0, &addr, &addrSize);
		if (size < 0) {
			if (NP_SockError() == NP_WouldBlock)
				break;
			if (NP_SockError() == NP_ConnReset)
				continue;
			NP_LastError = "Failed to receive from holepunch server";
			goto sockFail;
		}

		if (!size)
			for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
				if (!NutPunch_Memcmp(&addr, NP_Peers + i, addrSize)) {
					NP_KillPeer(i);
					goto recvNext;
				}

		for (int i = 0; i < sizeof(NP_ServiceTable) / sizeof(*NP_ServiceTable); i++) {
			const struct NP_ServicePacket entry = NP_ServiceTable[i];
			if (size < NUTPUNCH_HEADER_SIZE)
				break;
			if (!NutPunch_Memcmp(buf, entry.identifier, NUTPUNCH_HEADER_SIZE)) {
				entry.handler(
					addr, size - NUTPUNCH_HEADER_SIZE, (uint8_t*)(buf + NUTPUNCH_HEADER_SIZE));
				goto recvNext;
			}
		}

		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
			if (!NutPunch_Memcmp(&addr, NP_Peers + i, addrSize)) {
				connIdx = i;
				break;
			}
		}

		if (connIdx == NUTPUNCH_MAX_PLAYERS)
			continue;
		NP_QueueIn = (struct NP_Packet*)NutPunch_Malloc(sizeof(*next));
		NP_QueueIn->data = (char*)NutPunch_Malloc(size);
		NutPunch_Memcpy(NP_QueueIn->data, buf, size);
		NP_QueueIn->peer = connIdx;
		NP_QueueIn->size = size;
		NP_QueueIn->next = next;

	recvNext:
		continue;
	}

	static char bye[] = {'D', 'I', 'S', 'C'};
	if (!NP_Closing)
		goto sendAway;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		for (int kkk = 0; kkk < 10; kkk++)
			NutPunch_Send(i, bye, sizeof(bye));

sendAway:
	while (NP_QueueOut != NULL) {
		struct NP_Packet* packet = NP_QueueOut;
		NP_QueueOut = NP_QueueOut->next;

		if (!NutPunch_PeerAlive(packet->peer)) {
			NP_KillPeer(packet->peer);
			NutPunch_Free(packet->data);
			NutPunch_Free(packet);
			continue;
		}

		addr = NP_Peers[packet->peer];
		int result = sendto(NP_Socket, packet->data, packet->size, 0, &addr, addrSize);
		NutPunch_Free(packet->data);
		NutPunch_Free(packet);

		if (result < 0 && NP_SockError() != NP_WouldBlock) {
			if (NP_SockError() == NP_ConnReset)
				NP_KillPeer(packet->peer);
			else {
				NP_LastError = "Failed to send to peer";
				goto sockFail;
			}
		}
	}

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

	*size = NP_QueueIn->size;
	NutPunch_Memcpy(out, NP_QueueIn->data, *size);
	NutPunch_Free(NP_QueueIn->data);

	int sourcePeer = NP_QueueIn->peer;
	struct NP_Packet* next = NP_QueueIn->next;
	NutPunch_Free(NP_QueueIn);

	NP_QueueIn = next;
	return sourcePeer;
}

void NutPunch_Send(int peer, const void* data, int size) {
	if (NP_LastStatus != NP_Status_Online || !NutPunch_PeerAlive(peer) || NutPunch_LocalPeer() == peer)
		return;
	struct NP_Packet* next = NP_QueueOut;
	NP_QueueOut = (struct NP_Packet*)NutPunch_Malloc(sizeof(*next));
	NP_QueueOut->next = next;
	NP_QueueOut->peer = peer;
	NP_QueueOut->size = size;
	NP_QueueOut->data = (char*)NutPunch_Malloc(size);
	NutPunch_Memcpy(NP_QueueOut->data, data, size);
}

void NutPunch_FindLobbies(int filterCount, const struct NutPunch_Filter* filters) {
	NP_LazyInit();

	if (!NP_ServerHost[0]) {
		NP_LastError = "Holepuncher server address unset";
		NP_LastErrorCode = 0;
		goto fail;
	}

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

const char** NutPunch_LobbyList(int* count) {
	static const char nully[NUTPUNCH_ID_MAX + 1] = {0};
	*count = 0;
	while (*count < NUTPUNCH_SEARCH_RESULTS_MAX) {
		if (!NutPunch_Memcmp(NP_Lobbies[*count], nully, sizeof(nully)))
			break;
		*count += 1;
	}
	return (const char**)NP_Lobbies;
}

int NutPunch_PeerCount() {
	switch (NP_LastStatus) {
		case NP_Status_Idle:
		case NP_Status_Error:
			return 0;
		default:
			int count = 0;
			for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
				count += NutPunch_PeerAlive(i);
			return count;
	}
}

const void* NutPunch_ServerAddr() {
	return &NP_RemoteAddr;
}

bool NutPunch_PeerAlive(int peer) {
	if (NutPunch_LocalPeer() == peer)
		return true;
	return 0 != ((struct sockaddr_in*)(NP_Peers + peer))->sin_port;
}

int NutPunch_LocalPeer() {
	return NP_LocalPeer;
}

#endif

#ifdef __cplusplus
}
#endif
