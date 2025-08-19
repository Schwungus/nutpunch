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

#ifndef NUTPUNCH_MIN_PORT
/// Minimum port number to be punched.
#define NUTPUNCH_MIN_PORT (20000)
#endif

#ifndef NUTPUNCH_MAX_PORT
/// Maximum port number to be punched.
#define NUTPUNCH_MAX_PORT (30000)
#endif

#if NUTPUNCH_MIN_PORT >= NUTPUNCH_MAX_PORT
#error nutpunch: impossible min/max port
#endif

/// Maximum amount of players in a lobby. Not intended to be customizable.
#define NUTPUNCH_MAX_PLAYERS (16)
#define NUTPUNCH_PAYLOAD_SIZE (NUTPUNCH_MAX_PLAYERS * 6)

/// The UDP port used by the punching mediator server. Not customizable, sorry.
#define NUTPUNCH_SERVER_PORT (30001)

/// The maximum length of a lobby identifier excluding the null terminator. Not customizable.
#define NUTPUNCH_ID_MAX (64)

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
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
	NP_Status_Idle,
	NP_Status_InProgress,
	NP_Status_Punched,
	NP_Status_Error,
};

struct NutPunch {
	uint8_t addr[4];
	uint16_t port;
};

// Forward-declarations:

/// Generate a random port to punch through.
uint16_t NutPunch_GeneratePort();

/// Set a custom hole-puncher server address.
void NutPunch_SetServerAddr(const char*);

/// Initiate a nut-punch to the specified lobby. Query status by calling `NutPunch_Query()` every frame.
bool NutPunch_Join(const char*);

/// Run this at the end of your program to do semi-important cleanup.
void NutPunch_Cleanup();

/// Query the punching status. Upon returning `NP_Status_Punched`, populates the peers array.
int NutPunch_Query();

/// Release the internal socket and return the punched port to start listening on.
uint16_t NutPunch_Release();

/// Use this to reset the underlying socket in case of an inexplicable error.
void NutPunch_Reset();

/// Get the human-readable description of the latest `NutPunch_Query()` error.
const char* NutPunch_GetLastError();

/// Get the array of peers discovered by `NutPunch_Join()`. Updated every `NutPunch_Query()` call.
///
/// NOTE: Remote peers you can connect to start at index 1. The initial (index 0) element is always the remote
/// representation of the local peer.
///
/// TIP: 0th peer's port is the same as the one you would get from `NutPunch_Release()`, meaning you don't need to save
/// the result of that call for future use.
struct NutPunch* NutPunch_GetPeers();

/// Count the peers discovered in the lobby after `NutPunch_Join()`. Updated every `NutPunch_Query()` call.
///
/// Returns 0 in case of an error.
int NutPunch_GetPeerCount();

#ifdef NUTPUNCH_IMPLEMENTATION

#define NutPunch_Log(...)                                                                                              \
	do {                                                                                                           \
		fprintf(stderr, __VA_ARGS__);                                                                          \
		fprintf(stderr, "\n");                                                                                 \
		fflush(stderr);                                                                                        \
	} while (0)

static const char* NutPunch_LastError = NULL;
static int NutPunch_LastErrorCode = 0;

static bool NutPunch_HadInit = false;
static int NutPunch_LastStatus = NP_Status_Idle;

static char NutPunch_LobbyId[NUTPUNCH_ID_MAX + 1] = {0};
static struct NutPunch NutPunch_List[NUTPUNCH_MAX_PLAYERS] = {0};
static int NutPunch_Count = 0;

static SOCKET NutPunch_LocalSocket = INVALID_SOCKET;
static struct sockaddr NutPunch_RemoteAddr = {0};
static char NutPunch_ServerHost[128] = {0}, NutPunch_ServerPort[32] = {0};

static void NutPunch_NukePeersList() {
	NutPunch_Count = 0;
	memset(NutPunch_List, 0, sizeof(NutPunch_List));
}

static void NutPunch_NukeSocket() {
	if (NutPunch_LocalSocket != INVALID_SOCKET) {
		closesocket(NutPunch_LocalSocket);
		NutPunch_LocalSocket = INVALID_SOCKET;
	}

	NutPunch_LobbyId[0] = 0;
	memset(&NutPunch_RemoteAddr, 0, sizeof(NutPunch_RemoteAddr));
	memset(&NutPunch_ServerHost, 0, sizeof(NutPunch_ServerHost));
	memset(&NutPunch_ServerPort, 0, sizeof(NutPunch_ServerPort));
}

static void NutPunch_LazyInit() {
	if (NutPunch_HadInit)
		return;
	NutPunch_HadInit = true;

	WSADATA bitch = {0};
	WSAStartup(MAKEWORD(2, 2), &bitch);

	srand(time(NULL));
	NutPunch_LocalSocket = INVALID_SOCKET;

	NutPunch_NukePeersList();
}

static void NutPunch_PrintError() {
	if (NutPunch_LastErrorCode)
		NutPunch_Log("WARN: %s (error code: %d)", NutPunch_LastError, NutPunch_LastErrorCode);
	else
		NutPunch_Log("WARN: %s", NutPunch_LastError);
}

uint16_t NutPunch_GeneratePort() {
	NutPunch_LazyInit();
	return NUTPUNCH_MIN_PORT + rand() % (NUTPUNCH_MAX_PORT - NUTPUNCH_MIN_PORT + 1);
}

static struct sockaddr NutPunch_SockAddr(const char* host, uint16_t port) {
	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = port;
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
	NutPunch_NukePeersList();
	NutPunch_NukeSocket();
}

static bool NutPunch_BindSocket(uint16_t port) {
	if (NutPunch_LocalSocket != INVALID_SOCKET) {
		closesocket(NutPunch_LocalSocket);
		NutPunch_LocalSocket = INVALID_SOCKET;
	}

	NutPunch_LocalSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (NutPunch_LocalSocket == INVALID_SOCKET) {
		NutPunch_LastError = "Failed to create the underlying UDP socket";
		goto fail;
	}

	u_long argp = 1;
	if (setsockopt(NutPunch_LocalSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&argp, sizeof(argp))) {
		NutPunch_LastError = "Failed to set socket reuseaddr option";
		goto fail;
	}

	argp = 1;
	if (SOCKET_ERROR == ioctlsocket(NutPunch_LocalSocket, FIONBIO, &argp)) {
		NutPunch_LastError = "Failed to set socket to non-blocking mode";
		goto fail;
	}

	struct sockaddr addr = NutPunch_SockAddr(NULL, port);
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

		closesocket(NutPunch_LocalSocket);
		NutPunch_LocalSocket = INVALID_SOCKET;

		return false;
	}

	return res > 0;
}

bool NutPunch_Join(const char* lobby) {
	NutPunch_LazyInit();
	NutPunch_NukePeersList();

	if (!NutPunch_ServerHost[0]) {
		NutPunch_LastError = "Holepuncher server address unset";
		NutPunch_LastErrorCode = 0;
		NutPunch_PrintError();
		goto fail;
	}

	if (NutPunch_BindSocket(NutPunch_GeneratePort())) {
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
	if (NutPunch_LastStatus == NP_Status_InProgress
		&& SOCKET_ERROR
			   == sendto(NutPunch_LocalSocket, NutPunch_LobbyId, NUTPUNCH_ID_MAX, 0, &NutPunch_RemoteAddr,
				   sizeof(NutPunch_RemoteAddr))
		&& WSAGetLastError() != WSAEWOULDBLOCK)
	{
		NutPunch_LastError = "Failed to send heartbeat";
		goto sockFail;
	}

	static char buf[NUTPUNCH_PAYLOAD_SIZE] = {0};
	memset(buf, 0, sizeof(buf));

	struct sockaddr addr = NutPunch_RemoteAddr;
	int addrSize = sizeof(NutPunch_RemoteAddr);
	int nRecv = recvfrom(NutPunch_LocalSocket, buf, sizeof(buf), 0, &addr, &addrSize);

	if (SOCKET_ERROR == nRecv && WSAGetLastError() != WSAEWOULDBLOCK) {
		NutPunch_LastError = "Failed to receive from holepunch server";
		goto sockFail;
	}
	if (nRecv != NUTPUNCH_PAYLOAD_SIZE) // fucking skip invalid/partitioned packets
		return NP_Status_InProgress;

	NutPunch_Count = 0;
	uint8_t* ptr = (uint8_t*)buf;

	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		for (int j = 0; j < 4; j++)
			NutPunch_List[i].addr[j] = *ptr++;
		uint16_t portNE = ((uint16_t)(*ptr++));
		portNE |= ((uint16_t)(*ptr++) << 8);
		NutPunch_List[i].port = portNE;
		NutPunch_Count += portNE != 0;
	}

	return NP_Status_Punched;

sockFail:
	NutPunch_LastErrorCode = WSAGetLastError();
	NutPunch_NukePeersList();
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
		NutPunch_NukeSocket();
	}

	return NutPunch_LastStatus;
}

uint16_t NutPunch_Release() {
	if (NutPunch_LastStatus == NP_Status_Error)
		return 0;
	else {
		NutPunch_NukeSocket();
		NutPunch_LastStatus = NP_Status_Idle;
	}

	return NutPunch_GetPeerCount() ? NutPunch_List->port : 0;
}

struct NutPunch* NutPunch_GetPeers() {
	return NutPunch_List;
}

int NutPunch_GetPeerCount() {
	return NutPunch_LastStatus == NP_Status_Error ? 0 : NutPunch_Count;
}

#endif

#ifdef __cplusplus
}
#endif
