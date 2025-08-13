#pragma once

#include <winerror.h>
#if defined(_WIN32) || defined(_WIN64)
#define NUTPUNCH_WINDOSE
#else
#error OS not supported by nutpunch (yet)
#endif

#ifndef NUTPUNCH_MIN_PORT
/// Minimum port number to be punched.
#define NUTPUNCH_MIN_PORT (20000)
#endif

#ifndef NUTPUNCH_MAX_PORT
/// Maximum port number to be punched.
#define NUTPUNCH_MAX_PORT (30000)
#endif

#if NUTPUNCH_MIN_PORT > NUTPUNCH_MAX_PORT
#error nutpunch: impossible min/max port
#endif

/// Maximum amount of players in a lobby. Not intended to be customizable.
#define NUTPUNCH_MAX_PLAYERS (16)
#define NUTPUNCH_PAYLOAD_SIZE (NUTPUNCH_MAX_PLAYERS * 6)

/// The UDP port used by the punching mediator server. Not customizable, sorry.
#define NUTPUNCH_SERVER_PORT (24869)

/// The maximum length of a lobby identifier excluding the null terminator. Not customizable.
#define NUTPUNCH_ID_MAX (64)

#if defined(NUTPUNCH_IMPLEMENTATION) && defined(NUTPUNCH_WINDOSE)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
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
void NutPunch_Join(const char*);

/// Run this at the end of your program to do semi-important cleanup.
void NutPunch_Cleanup();

/// Query the punching status. Upon returning `NP_Status_Punched`, populates the peers array.
int NutPunch_Query();

/// Release the internal socket and return the punched port to start listening on.
uint16_t NutPunch_Release();

/// Get the human-readable description of the latest `NutPunch_Query()` error.
const char* NutPunch_GetLastError();

/// Get the array of peers discovered by `NutPunch_Join()`. Updated every `NutPunch_Query()` call.
///
/// NOTE: the initial element is ALWAYS the remote representation of the local peer.
const struct NutPunch* NutPunch_GetPeers();

/// Get the amount of peers discovered by `NutPunch_Join()`. Updated every `NutPunch_Query()` call.
int NutPunch_GetPeerCount();

// Implementation details:
#ifdef NUTPUNCH_IMPLEMENTATION

#define NutPunch_Log(...)                                                                                              \
	do {                                                                                                           \
		fprintf(stderr, __VA_ARGS__);                                                                          \
		fflush(stderr);                                                                                        \
	} while (0)

static const char* NutPunch_LastError = NULL;
static int NutPunch_LastErrorCode = 0;

static bool NutPunch_HadInit = false;
static int NutPunch_LastStatus = NP_Status_Idle;

static uint8_t NutPunch_LobbyId[NUTPUNCH_ID_MAX + 1] = {0};
static struct NutPunch NutPunch_List[NUTPUNCH_MAX_PLAYERS];
static size_t NutPunch_Count = 0;

static SOCKET NutPunch_LocalSock;
static struct sockaddr NutPunch_RemoteAddr = {0};
static char NutPunch_ServerHost[128] = {0}, NutPunch_ServerPort[32] = {0};

static void NutPunch_LazyInit() {
	if (!NutPunch_HadInit) {
		WSADATA bitch = {0};
		WSAStartup(MAKEWORD(2, 2), &bitch);

		srand(time(NULL));
		NutPunch_LocalSock = INVALID_SOCKET;
		NutPunch_HadInit = true;
	}
}

static void NutPunch_PrintError() {
	if (!NutPunch_LastErrorCode)
		NutPunch_Log("WARN: %s\n", NutPunch_LastError);
	else
		NutPunch_Log("WARN: %s (error code: %d)\n", NutPunch_LastError, NutPunch_LastErrorCode);
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
	if (hostname == NULL)
		NutPunch_ServerHost[0] = 0;
	else
		snprintf(NutPunch_ServerHost, sizeof(NutPunch_ServerHost), "%s", hostname);
	snprintf(NutPunch_ServerPort, sizeof(NutPunch_ServerPort), "%d", NUTPUNCH_SERVER_PORT);
}

static void NutPunch_CloseSocket() {
	if (NutPunch_LocalSock != INVALID_SOCKET) {
		closesocket(NutPunch_LocalSock);
		NutPunch_LocalSock = INVALID_SOCKET;
	}
}

static bool NutPunch_BindSocket(uint16_t port) {
	NutPunch_LocalSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (NutPunch_LocalSock == INVALID_SOCKET) {
		NutPunch_LastError = "Failed to create the underlying UDP socket";
		goto fail;
	}

	u_long argp = 1;
	if (setsockopt(NutPunch_LocalSock, SOL_SOCKET, SO_REUSEADDR, (char*)&argp, sizeof(argp))) {
		NutPunch_LastError = "Failed to set socked reuseaddr option";
		goto fail;
	}

	argp = 1;
	if (SOCKET_ERROR == ioctlsocket(NutPunch_LocalSock, FIONBIO, &argp)) {
		NutPunch_LastError = "Failed to make socket non-blocking";
		goto fail;
	}

	struct sockaddr addr = NutPunch_SockAddr(NULL, port);
	if (SOCKET_ERROR == bind(NutPunch_LocalSock, &addr, sizeof(addr))) {
		NutPunch_LastError = "Failed to bind the underlying UDP socket";
		goto fail;
	}

	NutPunch_LastStatus = NP_Status_InProgress;
	NutPunch_RemoteAddr = NutPunch_SockAddr(NutPunch_ServerHost, NUTPUNCH_SERVER_PORT);
	return true;

fail:
	NutPunch_LastErrorCode = WSAGetLastError();
	NutPunch_LastStatus = NP_Status_Error;
	NutPunch_PrintError();
	NutPunch_CloseSocket();
	return false;
}

static bool NutPunch_MayAccept() {
#ifdef NUTPUNCH_WINDOSE
	struct timeval instantBitchNoodles = {0};
	fd_set s = {1, {NutPunch_LocalSock}};
	NutPunch_LastErrorCode = select(0, &s, NULL, NULL, &instantBitchNoodles);

	if (NutPunch_LastErrorCode < 0) {
		NutPunch_LastErrorCode = WSAGetLastError();
		NutPunch_LastError = "Socket poll failed";
		NutPunch_PrintError();

		closesocket(NutPunch_LocalSock);
		NutPunch_LocalSock = INVALID_SOCKET;

		return 0;
	}

	return NutPunch_LastErrorCode > 0;
#else
#error Bad luck...
#endif
}

void NutPunch_Join(const char* lobby) {
	NutPunch_LazyInit();

	if (!NutPunch_ServerHost[0]) {
		NutPunch_LastError = "Holepuncher server address unset";
		NutPunch_LastErrorCode = 0;
		NutPunch_PrintError();

		NutPunch_LastStatus = NP_Status_Error;
		return;
	}

	if (NutPunch_BindSocket(NutPunch_GeneratePort())) {
		snprintf((char*)NutPunch_LobbyId, sizeof(NutPunch_LobbyId), "%s", lobby);
		NutPunch_RemoteAddr = NutPunch_SockAddr(NutPunch_ServerHost, NUTPUNCH_SERVER_PORT);
		NutPunch_LastStatus = NP_Status_Idle;
	} else
		NutPunch_LastStatus = NP_Status_Error;
}

void NutPunch_Cleanup() {
	NutPunch_CloseSocket();
	WSACleanup();
}

const char* NutPunch_GetLastError() {
	return NutPunch_LastError;
}

static int NutPunch_QueryImpl() {
	NutPunch_Count = 0;

	if (!NutPunch_LobbyId[0] || NutPunch_LocalSock == INVALID_SOCKET)
		return NP_Status_Idle;
	if (!NutPunch_ServerHost[0]) {
		NutPunch_LastErrorCode = 0;
		NutPunch_LastError = "Holepuncher server address unset";
		goto fail;
	}

	int remoteLen = sizeof(NutPunch_RemoteAddr);
	if (SOCKET_ERROR == sendto(NutPunch_LocalSock, (char*)NutPunch_LobbyId, NUTPUNCH_ID_MAX, 0,
				    &NutPunch_RemoteAddr, remoteLen)) {
		NutPunch_LastError = "Failed to send heartbeat";
		goto fail;
	}
	if (!NutPunch_MayAccept())
		return NP_Status_InProgress;

	uint8_t buf[NUTPUNCH_PAYLOAD_SIZE] = {0}, *ptr = buf;
	remoteLen = sizeof(NutPunch_RemoteAddr);
	int64_t nRecv = recvfrom(NutPunch_LocalSock, (char*)buf, sizeof(buf), 0, &NutPunch_RemoteAddr, &remoteLen);

	if (SOCKET_ERROR == nRecv) {
		if (NutPunch_LastErrorCode == WSAEWOULDBLOCK)
			return NP_Status_InProgress;
		NutPunch_LastError = "Failed to receive from holepunch server";
		goto fail;
	}
	if (nRecv < sizeof(buf)) // fucking skip invalid/partitioned packets
		return NP_Status_InProgress;

	for (size_t i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		for (size_t j = 0; j < 4; j++)
			NutPunch_List[i].addr[j] = *ptr++;
		uint16_t portNE = (uint16_t)(*ptr++) << 8;
		portNE |= (uint16_t)(*ptr++) << 0;
#ifdef __BIG_ENDIAN__
		portNE = (portNE >> 8) | (portNE << 8);
#endif
		NutPunch_List[i].port = portNE;
		NutPunch_Count += portNE != 0;
	}

	return NP_Status_Punched;

fail:
	NutPunch_LastErrorCode = WSAGetLastError();
	if (NutPunch_LastErrorCode == WSAEWOULDBLOCK)
		return NP_Status_InProgress;
	NutPunch_PrintError();

	NutPunch_Count = 0;
	memset(&NutPunch_RemoteAddr, 0, sizeof(NutPunch_RemoteAddr));
	memset(NutPunch_List, 0, sizeof(*NutPunch_List) * NUTPUNCH_MAX_PLAYERS);

	return NP_Status_Error;
}

int NutPunch_Query() {
	NutPunch_LazyInit();
	if (NutPunch_LastStatus == NP_Status_Error)
		return NP_Status_Error;
	else
		return (NutPunch_LastStatus = NutPunch_QueryImpl());
}

uint16_t NutPunch_Release() {
	NutPunch_LobbyId[0] = 0;
	NutPunch_CloseSocket();
	return NutPunch_GetPeerCount() ? NutPunch_List[0].port : 0;
}

const struct NutPunch* NutPunch_GetPeers() {
	return NutPunch_List;
}

int NutPunch_GetPeerCount() {
	return NutPunch_Count;
}

// The integrated hole-punch server:
#ifdef NUTPUNCH_COMPILE_SERVER

#define NUTPUNCH_LOBBY_MAX (16)
#define NUTPUNCH_HEARTBEAT_RATE (60)

struct NutPunch_Trailer {
	int heartbeat, addrLen;
	struct sockaddr addr;
};

struct NutPunch_Lobby {
	char identifier[NUTPUNCH_ID_MAX + 1];
	struct NutPunch players[NUTPUNCH_MAX_PLAYERS];
	struct NutPunch_Trailer trailers[NUTPUNCH_MAX_PLAYERS];
};

static struct NutPunch_Lobby lobbies[NUTPUNCH_LOBBY_MAX];

static void NutPunch_Serve_Init() {
	NutPunch_LazyInit();
	NutPunch_SetServerAddr(NULL);

	if (!NutPunch_BindSocket(NUTPUNCH_SERVER_PORT)) {
		NutPunch_LastError = "Server init failed";
		NutPunch_LastErrorCode = 0;
		NutPunch_PrintError();

		NutPunch_Cleanup();
		exit(EXIT_FAILURE);
	}
}

static void NutPunch_Serve_KillLobby(struct NutPunch_Lobby* lobby) {
	if (lobby->identifier[0])
		NutPunch_Log("Destroying lobby '%s'\n", lobby->identifier);
	memset(lobby->players, 0, sizeof(*lobby->players) * NUTPUNCH_MAX_PLAYERS);
	memset(lobby->trailers, 0, sizeof(*lobby->trailers) * NUTPUNCH_MAX_PLAYERS);
	memset(lobby->identifier, 0, NUTPUNCH_ID_MAX + 1);
}

static void NutPunch_Serve_Reset() {
	for (size_t i = 0; i < NUTPUNCH_LOBBY_MAX; i++)
		NutPunch_Serve_KillLobby(&lobbies[i]);
}

static void NutPunch_WritePayload(const struct NutPunch_Lobby* lobby, size_t playerIdx, uint8_t** ptr) {
	if (!lobby->trailers[playerIdx].heartbeat) {
		memset(*ptr, 0, 6);
		*ptr += 6;
	} else {
		memcpy(*ptr, lobby->players[playerIdx].addr, 4);
		*ptr += 4;

		uint16_t portLE = lobby->players[playerIdx].port;
#ifdef __BIG_ENDIAN__
		portLE = (portLE >> 8) | (portLE << 8);
#endif
		(*ptr)++[0] = (portLE & 0xFF00) >> 8;
		(*ptr)++[0] = (portLE & 0x00FF) >> 0;
	}
}

static void NutPunch_Serve_UpdateLobby(struct NutPunch_Lobby* lobby) {
	static uint8_t recvBuf[NUTPUNCH_ID_MAX + 1] = {0};

	for (size_t i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		if (lobby->trailers[i].heartbeat > 0)
			lobby->trailers[i].heartbeat -= 1;

		memset(recvBuf, 0, sizeof(recvBuf));
		int64_t nRecv = recvfrom(NutPunch_LocalSock, (char*)recvBuf, NUTPUNCH_ID_MAX, 0,
			&lobby->trailers[i].addr, &lobby->trailers[i].addrLen);

		if (nRecv == SOCKET_ERROR) {
			if (WSAGetLastError() != WSAEWOULDBLOCK)
				lobby->trailers[i].heartbeat = 0; // just kill him.....
		} else if (nRecv == NUTPUNCH_ID_MAX)
			lobby->trailers[i].heartbeat = NUTPUNCH_HEARTBEAT_RATE;
	}

	bool allGay = true;
	for (size_t i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		if (lobby->trailers[i].heartbeat) {
			allGay = false;
			break;
		}
	}
	if (allGay) {
		NutPunch_Serve_KillLobby(lobby);
		return;
	}

	for (size_t recptIdx = 0; recptIdx < NUTPUNCH_MAX_PLAYERS; recptIdx++) {
		if (!lobby->trailers[recptIdx].heartbeat)
			continue;

		unsigned char packet[NUTPUNCH_PAYLOAD_SIZE] = {0}, *ptr = packet;
		NutPunch_WritePayload(lobby, recptIdx, &ptr);

		for (size_t i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			if (i != recptIdx)
				NutPunch_WritePayload(lobby, i, &ptr);

		size_t junk = 0;

		if (SOCKET_ERROR == sendto(NutPunch_LocalSock, (char*)packet, sizeof(packet), 0,
					    &lobby->trailers[recptIdx].addr, lobby->trailers[recptIdx].addrLen)) {
			lobby->trailers[recptIdx].heartbeat = 0;
			NutPunch_LastErrorCode = WSAGetLastError();
			NutPunch_LastError = "Peer connection lost";
			NutPunch_PrintError();
		}
	}
}

static void NutPunch_Serve_UpdateLobbies() {
	for (struct NutPunch_Lobby* lobby = lobbies; lobby < lobbies + NUTPUNCH_LOBBY_MAX; lobby++)
		NutPunch_Serve_UpdateLobby(lobby);
}

static bool NutPunch_Serve_LobbyEmpty(const struct NutPunch_Lobby* lobby) {
	for (size_t i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		if (lobby->trailers[i].heartbeat > 0)
			return false;
	return true;
}

/// Update the hole-punch server. Needs to be run in a loop, preferrably at 60Hz; wastes CPU cycles otherwise.
void NutPunch_Serve() {
	if (!NutPunch_HadInit) {
		NutPunch_Serve_Reset();
		NutPunch_LazyInit();
		NutPunch_Serve_Init();
	}
	if (!NutPunch_MayAccept())
		goto skip;

	static uint8_t buf[NUTPUNCH_ID_MAX + 1] = {0};
	memset(buf, 0, sizeof(buf));
	size_t nRecv = 0;

	struct sockaddr_in clientAddr = {0};
	int addrLen = sizeof(clientAddr);

	nRecv = recvfrom(NutPunch_LocalSock, (char*)buf, NUTPUNCH_ID_MAX, 0, (struct sockaddr*)&clientAddr, &addrLen);
	if (SOCKET_ERROR == nRecv) {
		NutPunch_LastErrorCode = WSAGetLastError();
		if (NutPunch_LastErrorCode == WSAEWOULDBLOCK || NutPunch_LastErrorCode == WSAECONNRESET)
			goto skip;
		NutPunch_LastError = "Client socket recv error";
		NutPunch_PrintError();
		goto skip;
	}
	if (nRecv != NUTPUNCH_ID_MAX) // fucking skip you bitch!!!
		goto skip;

	struct NutPunch_Lobby* lobby = NULL; // find lobby by supplied ID
	for (size_t i = 0; i < NUTPUNCH_LOBBY_MAX; i++)
		if (!memcmp(lobbies[i].identifier, buf, NUTPUNCH_ID_MAX + 1)) {
			lobby = &lobbies[i];
			break;
		}

	size_t punch = NUTPUNCH_LOBBY_MAX;
	if (lobby == NULL) { // no such lobby; create it
		for (size_t i = 0; i < NUTPUNCH_LOBBY_MAX; i++)
			if (NutPunch_Serve_LobbyEmpty(&lobbies[i])) {
				lobby = &lobbies[i];
				break;
			}
		if (lobby == NULL) { // lobby list exhausted - needs a hard-reset
			NutPunch_Serve_Reset();
			goto skip;
		}

		memcpy(lobby->identifier, buf, NUTPUNCH_LOBBY_MAX + 1);
		NutPunch_Log("Lobby '%s' created!\n", buf);

		punch = 0;
		goto accept;
	}

	// Lobby found: join it.
	for (size_t i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		bool sameHost = !memcmp(&lobby->players[i].addr, &clientAddr.sin_addr, 4);
		bool samePort = lobby->players[i].port == clientAddr.sin_port;
		if (sameHost && samePort)
			goto skip; // already in
	}
	for (punch = 0; punch < NUTPUNCH_MAX_PLAYERS; punch++) {
		if (!lobby->trailers[punch].heartbeat) // i'm replacing your bitch ass
			goto accept;
	}

accept:
	if (punch >= NUTPUNCH_MAX_PLAYERS)
		goto skip;
	NutPunch_Log("New client is in...\n");

	memcpy(&lobby->players[punch].addr, &clientAddr.sin_addr, 4);
	lobby->players[punch].port = clientAddr.sin_port;

	lobby->trailers[punch].heartbeat = NUTPUNCH_HEARTBEAT_RATE;
	memcpy(&lobby->trailers[punch].addr, &clientAddr, addrLen);
	lobby->trailers[punch].addrLen = sizeof(struct sockaddr_in);

skip:
	NutPunch_Serve_UpdateLobbies();
}

#endif

#endif
