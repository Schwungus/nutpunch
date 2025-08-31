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
#define NUTPUNCH_ID_MAX (64)

/// How many bytes to reserve for every network packet.
#define NUTPUNCH_BUFFER_SIZE (512000)

#define NUTPUNCH_FIELD_NAME_MAX (8)
#define NUTPUNCH_FIELD_DATA_MAX (32)
#define NUTPUNCH_MAX_FIELDS (16)

#define NUTPUNCH_RESPONSE_SIZE                                                                                         \
	(NUTPUNCH_MAX_PLAYERS * sizeof(struct NutPunch) + NUTPUNCH_MAX_FIELDS * sizeof(struct NutPunch_Field))
#define NUTPUNCH_REQUEST_SIZE (NUTPUNCH_ID_MAX + NUTPUNCH_MAX_FIELDS * sizeof(struct NutPunch_Field))

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

// Forward-declarations:

/// Set a custom hole-puncher server address.
void NutPunch_SetServerAddr(const char* hostname);

/// Initiate a nut-punch to the specified lobby. Query status by calling `NutPunch_Query()` every frame.
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

/// Use this to reset the underlying socket in case of an inexplicable error.
void NutPunch_Reset();

/// Get the human-readable description of the latest error in `NutPunch_Update()`.
const char* NutPunch_GetLastError();

#define NutPunch_Log(...)                                                                                              \
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

static const char* NP_LastError = NULL;
static int NP_LastErrorCode = 0;

static bool NP_InitDone = false;
static int NP_LastStatus = NP_Status_Idle;

static char NP_LobbyId[NUTPUNCH_ID_MAX + 1] = {0};
static struct NutPunch NP_AvailPeers[NUTPUNCH_MAX_PLAYERS] = {0};
static struct sockaddr NP_Connections[NUTPUNCH_MAX_PLAYERS] = {0};

static NP_SocketType NP_Socket = NUTPUNCH_INVALID_SOCKET;
static struct sockaddr NP_RemoteAddr = {0};
static char NP_ServerHost[128] = {0};

static struct NP_Packet *NP_QueueIn = NULL, *NP_QueueOut = NULL;
static struct NutPunch_Field NP_MetadataIn[NUTPUNCH_MAX_FIELDS] = {0}, NP_MetadataOut[NUTPUNCH_MAX_FIELDS] = {0};

static void NutPunch_NukeLobbyData() {
	NutPunch_Memset(NP_AvailPeers, 0, sizeof(NP_AvailPeers));
	NutPunch_Memset(NP_MetadataIn, 0, sizeof(NP_MetadataIn));
	NutPunch_Memset(NP_MetadataOut, 0, sizeof(NP_MetadataOut));
	NutPunch_Memset(NP_Connections, 0, sizeof(NP_Connections));
}

static void NutPunch_NukeRemote() {
	NP_LobbyId[0] = 0;
	NutPunch_Memset(&NP_RemoteAddr, 0, sizeof(NP_RemoteAddr));
	NutPunch_Memset(NP_ServerHost, 0, sizeof(NP_ServerHost));
	NutPunch_Memset(NP_Connections, 0, sizeof(NP_Connections));
}

static void NutPunch_NukeSocket() {
	NutPunch_NukeLobbyData();
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

	NutPunch_NukeLobbyData();
}

static void NutPunch_PrintError() {
	if (NP_LastErrorCode)
		NutPunch_Log("WARN: %s (error code: %d)", NP_LastError, NP_LastErrorCode);
	else
		NutPunch_Log("WARN: %s", NP_LastError);
}

static struct sockaddr NutPunch_SockAddr(const char* host, uint16_t port) {
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
	NutPunch_NukeRemote();
	NutPunch_NukeSocket();
}

static int NutPunch_FieldNameSize(const char* name) {
	for (int i = 0; i < NUTPUNCH_FIELD_NAME_MAX; i++)
		if (!name[i])
			return i;
	return NUTPUNCH_FIELD_NAME_MAX;
}

void* NutPunch_Get(const char* name, int* size) {
	static char copy[NUTPUNCH_FIELD_DATA_MAX] = {0};

	int nameSize = NutPunch_FieldNameSize(name);
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
	int nameSize = NutPunch_FieldNameSize(name);
	if (!nameSize)
		return;
	if (nameSize > NUTPUNCH_FIELD_NAME_MAX)
		nameSize = NUTPUNCH_FIELD_NAME_MAX;

	if (dataSize <= 0) {
		NutPunch_Log("Invalid metadata field size!");
		return;
	}
	if (dataSize > NUTPUNCH_FIELD_DATA_MAX) {
		NutPunch_Log("WARN: trimming metadata field from %d to %d bytes", dataSize, NUTPUNCH_FIELD_DATA_MAX);
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

static bool NutPunch_BindSocket() {
	struct sockaddr addr;
#ifdef NUTPUNCH_WINDOSE
	u_long argp = 1;
#endif

	NutPunch_NukeSocket();

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

	addr = NutPunch_SockAddr(NULL, 0);
	if (bind(NP_Socket, &addr, sizeof(addr)) < 0) {
		NP_LastError = "Failed to bind the UDP socket";
		goto fail;
	}

	NP_LastStatus = NP_Status_Online;
	NP_RemoteAddr = NutPunch_SockAddr(NP_ServerHost, NUTPUNCH_SERVER_PORT);
	return true;

fail:
	NP_LastStatus = NP_Status_Error;
	NP_LastErrorCode = NP_SockError();
	NutPunch_PrintError();
	NutPunch_Reset();
	return false;
}

bool NutPunch_Join(const char* lobbyId) {
	NP_LazyInit();
	NutPunch_NukeLobbyData();

	if (!NP_ServerHost[0]) {
		NP_LastError = "Holepuncher server address unset";
		NP_LastErrorCode = 0;
		NutPunch_PrintError();
		goto fail;
	}

	if (NutPunch_BindSocket()) {
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

static void NP_CleanupPackets(struct NP_Packet** queue) {
	while (*queue != NULL) {
		struct NP_Packet* ptr = *queue;
		*queue = ptr->next;
		NutPunch_Free(ptr->data);
		NutPunch_Free(ptr);
	}
}

void NutPunch_Cleanup() {
	NP_CleanupPackets(&NP_QueueIn);
	NP_CleanupPackets(&NP_QueueOut);
	NutPunch_Reset();
#ifdef NUTPUNCH_WINDOSE
	WSACleanup();
#endif
}

const char* NutPunch_GetLastError() {
	return NP_LastError;
}

static int NutPunch_RealUpdate() {
	if (!NP_LobbyId[0] || NP_Socket == NUTPUNCH_INVALID_SOCKET)
		return NP_Status_Idle;

	static char introMagic[5] = "INTR", intro[6] = {0};
	NutPunch_Memcpy(intro, introMagic, sizeof(introMagic));

	struct sockaddr addr = NP_RemoteAddr;
	static char request[NUTPUNCH_REQUEST_SIZE] = {0};
	NutPunch_Memcpy(request, NP_LobbyId, NUTPUNCH_ID_MAX);
	NutPunch_Memcpy(request + NUTPUNCH_ID_MAX, NP_MetadataOut, sizeof(NP_MetadataOut));
	if (sendto(NP_Socket, request, sizeof(request), 0, &addr, sizeof(addr)) < 0 && NP_SockError() != NP_WouldBlock
		&& NP_SockError() != NP_ConnReset)
	{
		NP_LastError = "Failed to send heartbeat to nutpuncher server";
		goto sockFail;
	}

	for (;;) {
		struct sockaddr addr = {0};
#ifdef NUTPUNCH_WINDOSE
		int
#else
		socklen_t
#endif
			addrSize;
		addrSize = sizeof(addr);

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

		if (!NutPunch_Memcmp(&addr, &NP_RemoteAddr, sizeof(addr))) {
			if (size != NUTPUNCH_RESPONSE_SIZE)
				continue;
			uint8_t* ptr = (uint8_t*)buf;
			for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
				NutPunch_Memcpy(NP_AvailPeers[i].addr, ptr, 4);
				ptr += 4;
				NutPunch_Memcpy(&NP_AvailPeers[i].port, ptr, 2);
				ptr += 2;
			}
			NutPunch_Memcpy(NP_MetadataIn, ptr, sizeof(NP_MetadataIn));
		} else if (size == sizeof(intro) && !NutPunch_Memcmp(buf, introMagic, sizeof(introMagic))) {
			int connIdx = ((uint8_t*)buf)[sizeof(introMagic)];
			NP_Connections[connIdx] = addr;
		} else {
			int connIdx = NUTPUNCH_MAX_PLAYERS;
			for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
				if (!NutPunch_Memcmp(&addr, NP_Connections + i, sizeof(addr))) {
					connIdx = i;
					break;
				}
			}
		push:
			if (connIdx == NUTPUNCH_MAX_PLAYERS)
				continue;
			struct NP_Packet* next = NP_QueueIn;
			NP_QueueIn = (struct NP_Packet*)NutPunch_Malloc(sizeof(*next));
			NP_QueueIn->data = (char*)NutPunch_Malloc(size);
			NutPunch_Memcpy(NP_QueueIn->data, buf, size);
			NP_QueueIn->peer = connIdx;
			NP_QueueIn->size = size;
			NP_QueueIn->next = next;
		}
	}

	while (NP_QueueOut != NULL) {
		struct NP_Packet* packet = NP_QueueOut;
		if (!NutPunch_PeerAlive(packet->peer))
			break; // TODO: skip and retry on the next update
		NP_QueueOut = NP_QueueOut->next;

		struct sockaddr addr = NP_Connections[packet->peer];
		int result = sendto(NP_Socket, packet->data, packet->size, 0, &addr, sizeof(addr));
		NutPunch_Free(packet->data);
		NutPunch_Free(packet);

		if (result < 0 && NP_SockError() != NP_WouldBlock && NP_SockError() != NP_ConnReset) {
			NP_LastError = "Failed to send heartbeat";
			goto sockFail;
		}
	}

	intro[sizeof(introMagic)] = NutPunch_LocalPeer();
	if (intro[sizeof(introMagic)] == NUTPUNCH_MAX_PLAYERS)
		goto end;

	// Send introduction packets to everyone:
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		if (NP_AvailPeers[i].port && NutPunch_LocalPeer() != i) {
			struct sockaddr addr = {0};
			((struct sockaddr_in*)&addr)->sin_family = AF_INET;
			((struct sockaddr_in*)&addr)->sin_port = NP_AvailPeers[i].port;
			NutPunch_Memcpy(&((struct sockaddr_in*)&addr)->sin_addr, NP_AvailPeers[i].addr, 4);
			sendto(NP_Socket, intro, sizeof(intro), 0, &addr, sizeof(addr));
		}

end:
	return NP_Status_Online;

sockFail:
	NP_LastErrorCode = NP_SockError();
	NutPunch_NukeLobbyData();
	return NP_Status_Error;
}

int NutPunch_Update() {
	NP_LazyInit();

	if (NP_LastStatus != NP_Status_Idle)
		NP_LastStatus = NutPunch_RealUpdate();
	if (NP_LastStatus == NP_Status_Error) {
		NP_LastStatus = NP_Status_Idle;
		NutPunch_PrintError();
		NutPunch_NukeRemote();
		NutPunch_NukeSocket();
	}

	return NP_LastStatus;
}

bool NutPunch_HasNext() {
	return NP_QueueIn != NULL;
}

int NutPunch_NextPacket(void* out, int* size) {
	if (*size < NP_QueueIn->size) {
		NutPunch_Log("WARN: not enough memory allocated to copy the next packet");
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
	return 0 != ((struct sockaddr_in*)(NP_Connections + peer))->sin_port;
}

int NutPunch_LocalPeer() {
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		if (NP_AvailPeers[i].port && !*(uint32_t*)NP_AvailPeers[i].addr)
			return i;
	return NUTPUNCH_MAX_PLAYERS;
}

#endif

#ifdef __cplusplus
}
#endif
