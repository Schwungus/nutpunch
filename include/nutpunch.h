#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(_WIN64)
#define NUTPUNCH_WINDOSE
#else
#error OS not supported by nutpunch (yet)
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

/// Maximum amount of players in a lobby. Not intended to be customizable.
#define NUTPUNCH_MAX_PLAYERS (16)

/// The UDP port used by the punching mediator server. Not customizable, sorry.
#define NUTPUNCH_SERVER_PORT (30001)

/// The maximum length of a lobby identifier excluding the null terminator. Not customizable.
#define NUTPUNCH_ID_MAX (64)

#define NUTPUNCH_FIELD_NAME_MAX (8)
#define NUTPUNCH_FIELD_DATA_MAX (32)
#define NUTPUNCH_MAX_FIELDS (16)

#define NUTPUNCH_RESPONSE_SIZE                                                                                         \
	(NUTPUNCH_MAX_PLAYERS * sizeof(struct NutPunch) + NUTPUNCH_MAX_FIELDS * sizeof(struct NutPunch_Field))
#define NUTPUNCH_REQUEST_SIZE (NUTPUNCH_ID_MAX + NUTPUNCH_MAX_FIELDS * sizeof(struct NutPunch_Field))

#ifdef NUTPUNCH_WINDOSE

#ifndef WINVER
#define WINVER 0x0601
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#define _AMD64_
#define _INC_WINDOWS

#include <windef.h>

#include <minwinbase.h>
#include <winbase.h>

#include <winsock2.h>

#include <ws2tcpip.h>

#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
	NP_Status_Error,
	NP_Status_Idle,
	NP_Status_InProgress,
	NP_Status_Punched,
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
void NutPunch_SetServerAddr(const char*);

/// Initiate a nut-punch to the specified lobby. Query status by calling `NutPunch_Query()` every frame.
bool NutPunch_Join(const char*);

/// Run this at the end of your program to do semi-important cleanup.
void NutPunch_Cleanup();

/// Request the lobby to start the game. Only works if you're the lobby's master, but it's safe to call either way.
void NutPunch_Start();

/// Query the punching status.
///
/// Status `NP_Status_Punched` populates the peers array, and it means you're OK to call `NutPunch_Done()`.
int NutPunch_Query();

/// Return the nutpunched socket for P2P networking. Will return `INVALID_SOCKET` unless `NutPunch_Query()` gave a
/// `NP_Status_Punched` response.
///
/// WARNING: I repeat, you MUST use this exact socket for further networking; else the port we just punched will close.
SOCKET NutPunch_Done();

/// Request lobby metadata to be set. Can be called multiple times in a row. Will send out metadata changes on
/// `NutPunch_Query()`, and won't do anything unless you're the lobby's master.
///
/// See `NUTPUNCH_FIELD_NAME_MAX` and `NUTPUNCH_FIELD_DATA_MAX` for limitations on the amount of data you can squeeze.
void NutPunch_Set(const char*, int, const char*);

/// Request lobby metadata. Set `size` to the field's actual size if the pointer isn't `NULL`.
///
/// The resulting pointer is actually a static allocation, so don't rely too much on it; its data will change after the
/// next `NutPunch_Get` call.
const char* NutPunch_Get(const char*, int* size);

/// Use this to reset the underlying socket in case of an inexplicable error.
void NutPunch_Reset();

/// Get the human-readable description of the latest `NutPunch_Query()` error.
const char* NutPunch_GetLastError();

/// Get the array of peers discovered after `NutPunch_Join()`. Updated every `NutPunch_Query()` call.
///
/// Use `NutPunch_LocalPeer()` to get your index in the array.
struct NutPunch* NutPunch_GetPeers();

/// Count the peers discovered in the lobby after `NutPunch_Join()`. Updated every `NutPunch_Query()` call.
///
/// Returns 0 in case of an error.
int NutPunch_GetPeerCount();

/// Return the index of our peer.
int NutPunch_LocalPeer();

#define NutPunch_Log(...)                                                                                              \
	do {                                                                                                           \
		fprintf(stdout, __VA_ARGS__);                                                                          \
		fprintf(stdout, "\n");                                                                                 \
		fflush(stdout);                                                                                        \
	} while (0)

#ifdef NUTPUNCH_WINDOSE
#define NutPunch_SleepMs(ms) (Sleep((ms)))
#else
#error Bad luck.
#endif

#ifdef NUTPUNCH_IMPLEMENTATION

static struct NutPunch_Field NutPunch_ReceivedMetadata[NUTPUNCH_MAX_FIELDS] = {0},
			     NutPunch_PendingMetadata[NUTPUNCH_MAX_FIELDS] = {0};

static const char* NutPunch_LastError = NULL;
static int NutPunch_LastErrorCode = 0;

static bool NutPunch_HadInit = false;
static int NutPunch_LastStatus = NP_Status_Idle;

static char NutPunch_LobbyId[NUTPUNCH_ID_MAX + 1] = {0};
static struct NutPunch NutPunch_Peers[NUTPUNCH_MAX_PLAYERS] = {0};
static int NutPunch_Count = 0;

static SOCKET NutPunch_LocalSocket = INVALID_SOCKET;
static struct sockaddr NutPunch_RemoteAddr = {0};
static char NutPunch_ServerHost[128] = {0}, NutPunch_ServerPort[32] = {0};

static void NutPunch_NukeLobbyData() {
	NutPunch_Count = 0;
	memset(NutPunch_Peers, 0, sizeof(NutPunch_Peers));
	memset(NutPunch_ReceivedMetadata, 0, sizeof(NutPunch_ReceivedMetadata));
	memset(NutPunch_PendingMetadata, 0, sizeof(NutPunch_PendingMetadata));
}

static void NutPunch_NukeRemote() {
	NutPunch_LobbyId[0] = 0;
	memset(&NutPunch_RemoteAddr, 0, sizeof(NutPunch_RemoteAddr));
	memset(&NutPunch_ServerHost, 0, sizeof(NutPunch_ServerHost));
	memset(&NutPunch_ServerPort, 0, sizeof(NutPunch_ServerPort));
}

static void NutPunch_NukeSocket() {
	if (NutPunch_LocalSocket != INVALID_SOCKET) {
		closesocket(NutPunch_LocalSocket);
		NutPunch_LocalSocket = INVALID_SOCKET;
	}
}

static void NutPunch_LazyInit() {
	if (NutPunch_HadInit)
		return;
	NutPunch_HadInit = true;

	WSADATA bitch = {0};
	WSAStartup(MAKEWORD(2, 2), &bitch);
	NutPunch_LocalSocket = INVALID_SOCKET;

	NutPunch_NukeLobbyData();
}

static void NutPunch_PrintError() {
	if (NutPunch_LastErrorCode)
		NutPunch_Log("WARN: %s (error code: %d)", NutPunch_LastError, NutPunch_LastErrorCode);
	else
		NutPunch_Log("WARN: %s", NutPunch_LastError);
}

static struct sockaddr NutPunch_SockAddr(const char* host, uint16_t port) {
	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (host != NULL)
		inet_pton(addr.sin_family, host, &addr.sin_addr);
	return *(struct sockaddr*)&addr;
}

void NutPunch_SetServerAddr(const char* hostname) {
	NutPunch_LazyInit();
	if (hostname == NULL)
		NutPunch_ServerHost[0] = 0;
	else
		snprintf(NutPunch_ServerHost, sizeof(NutPunch_ServerHost), "%s", hostname);
	snprintf(NutPunch_ServerPort, sizeof(NutPunch_ServerPort), "%d", NUTPUNCH_SERVER_PORT);
}

void NutPunch_Reset() {
	NutPunch_NukeLobbyData();
	NutPunch_NukeRemote();
	NutPunch_NukeSocket();
}

const char* NutPunch_Get(const char* name, int* size) {
	static char copy[NUTPUNCH_FIELD_DATA_MAX] = {0};
	memset(copy, 0, NUTPUNCH_FIELD_DATA_MAX);

	int nameLen = strlen(name), nameSize = NUTPUNCH_FIELD_NAME_MAX;
	if (nameLen < nameSize)
		nameSize = nameLen;

	for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++)
		if (!memcmp(NutPunch_ReceivedMetadata[i].name, name, nameSize)) {
			*size = NutPunch_ReceivedMetadata[i].size;
			memcpy(copy, NutPunch_ReceivedMetadata[i].data, *size);
			return copy;
		}

	*size = 0;
	return copy;
}

void NutPunch_Set(const char* name, int dataSize, const char* data) {
	if (dataSize <= 0) {
		NutPunch_Log("Invalid metadata field size!");
		return;
	}
	if (dataSize > NUTPUNCH_FIELD_DATA_MAX) {
		NutPunch_Log("WARN: trimming metadata field from %d to %d bytes", dataSize, NUTPUNCH_FIELD_DATA_MAX);
		dataSize = NUTPUNCH_FIELD_DATA_MAX;
	}

	int nameLen = strlen(name), nameSize = NUTPUNCH_FIELD_NAME_MAX;
	if (nameLen < nameSize)
		nameSize = nameLen;

	static struct NutPunch_Field nullfield = {0};
	for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
		struct NutPunch_Field* ptr = &NutPunch_PendingMetadata[i];
		if (!memcmp(ptr->name, name, nameSize) || !memcmp(ptr, &nullfield, sizeof(nullfield))) {
			memcpy(ptr->name, name, nameSize);
			memcpy(ptr->data, data, dataSize);
			ptr->size = dataSize;
			break;
		}
	}
}

static bool NutPunch_BindSocket(uint16_t port) {
	struct sockaddr addr;
	u_long argp = 1;

	NutPunch_NukeSocket();

	NutPunch_LocalSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (NutPunch_LocalSocket == INVALID_SOCKET) {
		NutPunch_LastError = "Failed to create the underlying UDP socket";
		goto fail;
	}

	if (SOCKET_ERROR == ioctlsocket(NutPunch_LocalSocket, FIONBIO, &argp)) {
		NutPunch_LastError = "Failed to set socket to non-blocking mode";
		goto fail;
	}

	addr = NutPunch_SockAddr(NULL, port);
	if (SOCKET_ERROR == bind(NutPunch_LocalSocket, &addr, sizeof(addr))) {
		NutPunch_LastError = "Failed to bind the UDP socket";
		goto fail;
	}

	NutPunch_LastStatus = NP_Status_InProgress;
	NutPunch_RemoteAddr = NutPunch_SockAddr(NutPunch_ServerHost, NUTPUNCH_SERVER_PORT);
	return true;

fail:
	NutPunch_LastStatus = NP_Status_Error;
	NutPunch_LastErrorCode = WSAGetLastError();
	NutPunch_PrintError();
	NutPunch_Reset();
	return false;
}

static bool NutPunch_MayAccept() {
	if (NutPunch_LocalSocket == INVALID_SOCKET)
		return false;

	static struct timeval instantBitchNoodles = {0, 0};
	fd_set s = {1, {NutPunch_LocalSocket}};
	int res = select(0, &s, NULL, NULL, &instantBitchNoodles);

	if (res == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
		NutPunch_LastErrorCode = WSAGetLastError();
		NutPunch_LastError = "Socket poll failed";
		NutPunch_PrintError();
		NutPunch_NukeSocket();
		return false;
	}

	return res > 0;
}

bool NutPunch_Join(const char* lobby) {
	NutPunch_LazyInit();
	NutPunch_NukeLobbyData();

	if (!NutPunch_ServerHost[0]) {
		NutPunch_LastError = "Holepuncher server address unset";
		NutPunch_LastErrorCode = 0;
		NutPunch_PrintError();
		goto fail;
	}

	if (NutPunch_BindSocket(0)) {
		NutPunch_LastStatus = NP_Status_InProgress;
		memset(NutPunch_LobbyId, 0, sizeof(NutPunch_LobbyId));
		snprintf(NutPunch_LobbyId, sizeof(NutPunch_LobbyId), "%s", lobby);
		return true;
	}

fail:
	NutPunch_Reset();
	NutPunch_LastStatus = NP_Status_Error;
	return false;
}

void NutPunch_Cleanup() {
	NutPunch_Reset();
	WSACleanup();
}

const char* NutPunch_GetLastError() {
	return NutPunch_LastError;
}

static int NutPunch_QueryImpl() {
	if (!NutPunch_LobbyId[0] || NutPunch_LocalSocket == INVALID_SOCKET)
		return NP_Status_Idle;

	static char request[NUTPUNCH_REQUEST_SIZE] = {0}, response[NUTPUNCH_RESPONSE_SIZE] = {0};
	struct sockaddr addr;
	int addrSize, nRecv;
	uint8_t* ptr;

	memset(request, 0, sizeof(request));
	memcpy(request, NutPunch_LobbyId, NUTPUNCH_ID_MAX);
	memcpy(request + NUTPUNCH_ID_MAX, &NutPunch_PendingMetadata, sizeof(NutPunch_PendingMetadata));

	if ((NutPunch_LastStatus == NP_Status_InProgress || NutPunch_LastStatus == NP_Status_Punched)
		&& SOCKET_ERROR
			   == sendto(NutPunch_LocalSocket, request, sizeof(request), 0, &NutPunch_RemoteAddr,
				   sizeof(NutPunch_RemoteAddr))
		&& WSAGetLastError() != WSAEWOULDBLOCK)
	{
		NutPunch_LastError = "Failed to send heartbeat";
		goto sockFail;
	}

	memset(response, 0, sizeof(response));
	addr = NutPunch_RemoteAddr;
	addrSize = sizeof(NutPunch_RemoteAddr);
	nRecv = recvfrom(NutPunch_LocalSocket, response, sizeof(response), 0, &addr, &addrSize);

	if (SOCKET_ERROR == nRecv && WSAGetLastError() != WSAEWOULDBLOCK) {
		NutPunch_LastError = "Failed to receive from holepunch server";
		goto sockFail;
	}
	if (nRecv != NUTPUNCH_RESPONSE_SIZE) // fucking skip invalid/partitioned packets
		return NP_Status_InProgress;

	NutPunch_Count = 0;
	ptr = (uint8_t*)response;

	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		memcpy(NutPunch_Peers[i].addr, ptr, 4);
		ptr += 4;

		memcpy(&NutPunch_Peers[i].port, ptr, 2);
		NutPunch_Peers[i].port = ntohs(NutPunch_Peers[i].port);
		ptr += 2;

		NutPunch_Count += NutPunch_Peers[i].port != 0;
	}

	static struct NutPunch_Field nullfield = {0};
	for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
		if (memcmp(ptr, &nullfield, sizeof(nullfield)))
			memcpy(&NutPunch_ReceivedMetadata[i], ptr, sizeof(nullfield));
		ptr += sizeof(nullfield);
	}

	return NP_Status_Punched;

sockFail:
	NutPunch_LastErrorCode = WSAGetLastError();
	NutPunch_NukeLobbyData();
	return NP_Status_Error;
}

int NutPunch_Query() {
	NutPunch_LazyInit();

	if (NutPunch_LastStatus == NP_Status_Idle)
		return NP_Status_Idle;

	if (NutPunch_LastStatus == NP_Status_Error) {
		NutPunch_LastStatus = NP_Status_Idle;
		return NP_Status_Error;
	}

	NutPunch_LastStatus = NutPunch_QueryImpl();
	if (NutPunch_LastStatus == NP_Status_Error) {
		NutPunch_PrintError();
		NutPunch_NukeRemote();
		NutPunch_NukeSocket();
	}

	return NutPunch_LastStatus;
}

SOCKET NutPunch_Done() {
	if (NutPunch_LastStatus != NP_Status_Punched || !NutPunch_GetPeerCount())
		return INVALID_SOCKET;
	NutPunch_NukeRemote();
	NutPunch_LastStatus = NP_Status_Idle;
	return NutPunch_LocalSocket;
}

struct NutPunch* NutPunch_GetPeers() {
	return NutPunch_Peers;
}

int NutPunch_GetPeerCount() {
	return NutPunch_LastStatus == NP_Status_Error ? 0 : NutPunch_Count;
}

int NutPunch_LocalPeer() {
	for (int i = 0; i < NutPunch_GetPeerCount(); i++)
		if (0xFFFFFFFF == *(uint32_t*)&NutPunch_GetPeers()[i].addr)
			return i;
	return 0;
}

#endif

#ifdef __cplusplus
}
#endif
