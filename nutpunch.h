#pragma once

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

/// How many `NutPunch_Query` calls to send a heartbeat.
#define NUTPUNCH_SEND_INTERVAL (10)

#ifdef NUTPUNCH_WINDOSE

#pragma comment(lib, "Ws2_32.lib")

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <windows.h>
#include <winerror.h>
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

static char NutPunch_ServerAddr[512] = {0}; /* TODO: add a default value... */
static char NutPunch_ServerPort[64] = {0};
static bool NutPunch_Srand = false;

static bool NutPunch_WsaInit = false;
static int NutPunch_LastStatus = NP_Status_Idle;

static char NutPunch_LobbyId[NUTPUNCH_ID_MAX + 1] = {0};
static struct NutPunch NutPunch_List[NUTPUNCH_MAX_PLAYERS];
static size_t NutPunch_Count = 0;

static SOCKET NutPunch_LocalSock = INVALID_SOCKET;
static struct sockaddr NutPunch_RemoteAddr = {0};

uint16_t NutPunch_GeneratePort() {
	if (!NutPunch_Srand) {
		srand(time(NULL));
		NutPunch_Srand = true;
	}
	return NUTPUNCH_MIN_PORT + rand() % (NUTPUNCH_MAX_PORT - NUTPUNCH_MIN_PORT + 1);
}

void NutPunch_SetServerAddr(const char* addr) {
	if (addr == NULL)
		NutPunch_ServerAddr[0] = 0;
	else
		snprintf(NutPunch_ServerAddr, sizeof(NutPunch_ServerAddr), "%s", addr);
	snprintf(NutPunch_ServerPort, sizeof(NutPunch_ServerPort), "%d", NUTPUNCH_SERVER_PORT);
}

static void NutPunch_InitWsa() {
	if (!NutPunch_WsaInit) {
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData)) {
			NutPunch_Log("FATAL: WSA failed to initialize\n");
			exit(EXIT_FAILURE);
		}
		NutPunch_WsaInit = true;
	}
}

static void NutPunch_PrintWsaError() {
	NutPunch_Log("WARN: \"%s\" caused by:\n", NutPunch_LastError);
	NutPunch_Log("WARN: WSA error %d\n", WSAGetLastError());
}

static struct sockaddr NutPunch_SockAddr(const char* host, uint16_t port) {
	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = port;
	if (host != NULL)
		inet_pton(addr.sin_family, host, &addr.sin_addr);
	return *(struct sockaddr*)&addr;
}

static bool NutPunch_CreateSocket() {
	NutPunch_LocalSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (NutPunch_LocalSock == INVALID_SOCKET) {
		NutPunch_LastError = "Failed to create underlying UDP socket";
		goto fail;
	}

	DWORD arg = 1; // nonblocking
	if (ioctlsocket(NutPunch_LocalSock, FIONBIO, &arg)) {
		NutPunch_LastError = "Failed to make the UDP socket non-blocking";
		goto fail;
	}

	return true;

fail:
	if (NutPunch_LocalSock != INVALID_SOCKET)
		closesocket(NutPunch_LocalSock);
	NutPunch_LocalSock = INVALID_SOCKET;
	NutPunch_LastStatus = NP_Status_Error;
	NutPunch_PrintWsaError();

	return false;
}

static int NutPunch_SockStatus() {
	struct timeval instantBitchNoodles = {0};
	fd_set s = {1, {NutPunch_LocalSock}};
	return select(0, &s, NULL, NULL, &instantBitchNoodles);
}

void NutPunch_Join(const char* lobby) {
	NutPunch_InitWsa();

	if (!NutPunch_ServerPort[0]) {
		NutPunch_LastError = "Holepuncher server address/port unset";
		goto fail;
	}

	snprintf(NutPunch_LobbyId, sizeof(NutPunch_LobbyId), "%s", lobby);
	NutPunch_LastStatus = NP_Status_Idle;

	if (!NutPunch_CreateSocket()) {
		NutPunch_LastError = "Failed to create the UDP socket";
		goto fail;
	}

	struct sockaddr addr = NutPunch_SockAddr(NULL, NutPunch_GeneratePort());
	if (0 > bind(NutPunch_LocalSock, &addr, sizeof(addr))) {
		NutPunch_LastError = "Failed to bind to punching port";
		goto fail;
	}

	NutPunch_LastStatus = NP_Status_InProgress;
	NutPunch_RemoteAddr = NutPunch_SockAddr(NutPunch_ServerAddr, NUTPUNCH_SERVER_PORT);
	return;

fail:
	NutPunch_LastStatus = NP_Status_Error;
	NutPunch_PrintWsaError();
	return;
}

void NutPunch_Cleanup() {
	if (NutPunch_LocalSock != INVALID_SOCKET) {
		closesocket(NutPunch_LocalSock);
		NutPunch_LocalSock = INVALID_SOCKET;
	}

	WSACleanup();
}

const char* NutPunch_GetLastError() {
	return NutPunch_LastError;
}

static int NutPunch_QueryImpl() {
	NutPunch_Count = 0;

	if (!strlen(NutPunch_LobbyId) || NutPunch_LocalSock == INVALID_SOCKET)
		return NP_Status_Idle;
	if (!strlen(NutPunch_ServerAddr)) {
		NutPunch_LastError =
		    "NutPunch server address unset. Maybe you forgot to call `NutPunch_SetServerAddr()`?";
		goto fail;
	}

	static uint64_t queryCount = 0;
	if (!(queryCount++ % NUTPUNCH_SEND_INTERVAL)) {
		int result = sendto(
		    NutPunch_LocalSock, NutPunch_LobbyId, NUTPUNCH_ID_MAX, 0, (struct sockaddr*)&NutPunch_RemoteAddr,
		    sizeof(NutPunch_RemoteAddr)
		);
		if (result < 0) {
			queryCount = 0;
			NutPunch_LastError = "Failed to send lobby ID";
			goto fail;
		}
	}

	int status = NutPunch_SockStatus(), junk = sizeof(NutPunch_RemoteAddr);
	if (!status)
		return NP_Status_InProgress;
	if (status < 0) {
		NutPunch_LastError = "Cannot receive from holepunch server";
		goto fail;
	}

	char buf[NUTPUNCH_PAYLOAD_SIZE] = {0}, *ptr = buf;
	if (0 > recvfrom(NutPunch_LocalSock, buf, sizeof(buf), 0, (struct sockaddr*)&NutPunch_RemoteAddr, &junk)) {
		if (WSAGetLastError() == WSAEWOULDBLOCK)
			return NP_Status_InProgress;
		NutPunch_LastError = "Failed to receive from holepunch server";
		goto fail;
	}

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
	NutPunch_PrintWsaError();

	NutPunch_Count = 0;
	memset(&NutPunch_RemoteAddr, 0, sizeof(NutPunch_RemoteAddr));
	memset(NutPunch_List, 0, sizeof(struct NutPunch) * NUTPUNCH_MAX_PLAYERS);

	return NP_Status_Error;
}

int NutPunch_Query() {
	NutPunch_InitWsa();
	if (NutPunch_LastStatus == NP_Status_Error)
		return NP_Status_Error;
	else
		return (NutPunch_LastStatus = NutPunch_QueryImpl());
}

uint16_t NutPunch_Release() {
	NutPunch_LobbyId[0] = 0;
	if (NutPunch_LocalSock != INVALID_SOCKET)
		closesocket(NutPunch_LocalSock);
	NutPunch_LocalSock = INVALID_SOCKET;
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
	int heartbeat;
};

struct NutPunch_Lobby {
	char identifier[NUTPUNCH_ID_MAX + 1];
	struct NutPunch players[NUTPUNCH_MAX_PLAYERS];
	struct NutPunch_Trailer trailers[NUTPUNCH_MAX_PLAYERS];
};

static struct NutPunch_Lobby lobbies[NUTPUNCH_LOBBY_MAX];

static void NutPunch_Serve_Init() {
	NutPunch_SetServerAddr(NULL);

	if (!NutPunch_CreateSocket()) {
		NutPunch_LastError = "Failed to create the listener socket";
		goto fail;
	}

	struct sockaddr local = NutPunch_SockAddr(NULL, NUTPUNCH_SERVER_PORT);
	if (0 > bind(NutPunch_LocalSock, &local, sizeof(local))) {
		NutPunch_LastError = "Failed to bind to the listener socket";
		goto fail;
	}

	return;

fail:
	NutPunch_PrintWsaError();
	WSACleanup();
	exit(EXIT_FAILURE);
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

static void NutPunch_Serve_UpdateLobbies() {
	static char recvBuf[NUTPUNCH_ID_MAX + 1] = {0};

	for (struct NutPunch_Lobby* lobby = lobbies; lobby < lobbies + NUTPUNCH_LOBBY_MAX; lobby++) {
		for (size_t i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
			if (lobby->trailers[i].heartbeat > 0)
				lobby->trailers[i].heartbeat -= 1;

			struct sockaddr_in addr = {0};
			int addrLen = sizeof(addr);

			addr.sin_family = AF_INET;
			memcpy(&addr.sin_addr, lobby->players[i].addr, 4);
			addr.sin_port = lobby->players[i].port;

			memset(recvBuf, 0, sizeof(recvBuf));
			int result = recvfrom(
			    NutPunch_LocalSock, recvBuf, NUTPUNCH_ID_MAX, 0, (struct sockaddr*)&addr, &addrLen
			);
			if (!result)
				continue;
			if (result < 0) {
				if (WSAGetLastError() == WSAEWOULDBLOCK)
					continue;
				lobby->trailers[i].heartbeat = 0; // just kill him.....
			} else
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
			continue;
		}

		for (size_t recptIdx = 0; recptIdx < NUTPUNCH_MAX_PLAYERS; recptIdx++) {
			if (!lobby->trailers[recptIdx].heartbeat)
				continue;

			unsigned char packet[NUTPUNCH_PAYLOAD_SIZE] = {0}, *ptr = packet;
			NutPunch_WritePayload(lobby, recptIdx, &ptr);

			for (size_t i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
				if (i != recptIdx)
					NutPunch_WritePayload(lobby, i, &ptr);

			struct sockaddr_in addr = {0};
			int addrLen = sizeof(addr);

			addr.sin_family = AF_INET;
			memcpy(&addr.sin_addr, lobby->players[recptIdx].addr, 4);
			addr.sin_port = lobby->players[recptIdx].port;

			if (0 > sendto(
				    NutPunch_LocalSock, (const char*)packet, sizeof(packet), 0, (struct sockaddr*)&addr,
				    sizeof(addr)
				))
				lobby->trailers[recptIdx].heartbeat = 0;
		}
	}
}

static bool NutPunch_Serve_LobbyEmpty(const struct NutPunch_Lobby* lobby) {
	for (size_t i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		if (lobby->trailers[i].heartbeat > 0)
			return false;
	return true;
}

/// Update the hole-punch server. Needs to be run in a loop, preferrably at 50Hz; otherwise, it's a waste of CPU
/// cycles.
void NutPunch_Serve() {
	if (!NutPunch_WsaInit) {
		NutPunch_Serve_Reset();
		NutPunch_InitWsa();
		NutPunch_Serve_Init();
	}

	int result = NutPunch_SockStatus();
	if (!result)
		goto skip;
	if (result < 0) {
		NutPunch_LastError = "Server socket select error";
		NutPunch_PrintWsaError();
		goto skip;
	}

	struct sockaddr_in clientAddr = {0};
	int clientAddrLen = sizeof(clientAddr);

	static char buf[NUTPUNCH_ID_MAX + 1] = {0};
	memset(buf, 0, sizeof(buf));

	result = recvfrom(NutPunch_LocalSock, buf, NUTPUNCH_ID_MAX, 0, (struct sockaddr*)&clientAddr, &clientAddrLen);
	if (result < 0) {
		if (WSAGetLastError() != WSAEWOULDBLOCK || WSAGetLastError() != WSAECONNRESET) {
			NutPunch_LastError = "Client socket recv error";
			NutPunch_PrintWsaError();
		}
	}
	if (result <= 0)
		goto skip;

	struct NutPunch_Lobby* perfectLobby = NULL; // find lobby by supplied ID
	for (size_t i = 0; i < NUTPUNCH_LOBBY_MAX; i++)
		if (!memcmp(lobbies[i].identifier, buf, NUTPUNCH_ID_MAX + 1)) {
			perfectLobby = &lobbies[i];
			break;
		}

	size_t punch = NUTPUNCH_LOBBY_MAX;
	if (perfectLobby == NULL) { // no such lobby; create it
		for (size_t i = 0; i < NUTPUNCH_LOBBY_MAX; i++)
			if (NutPunch_Serve_LobbyEmpty(&lobbies[i])) {
				perfectLobby = &lobbies[i];
				break;
			}
		if (perfectLobby == NULL) { // lobby list exhausted - needs a hard-reset
			NutPunch_Serve_Reset();
			goto skip;
		}

		memcpy(perfectLobby->identifier, buf, NUTPUNCH_LOBBY_MAX + 1);
		punch = 0;

		NutPunch_Log("Lobby '%s' created!\n", buf);
		goto accept;
	}

	// Lobby found. Join it.
	for (size_t i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		bool sameAddr = !memcmp(perfectLobby->players[i].addr, &clientAddr.sin_addr, 4);
		bool samePort = perfectLobby->players[i].port == clientAddr.sin_port;

		if (sameAddr && samePort)
			goto skip; // already in
		if (!perfectLobby->trailers[i].heartbeat) {
			punch = i;
			goto accept;
		}
	}

	// No idea what happened...
	goto skip;

accept:
	if (punch >= NUTPUNCH_MAX_PLAYERS)
		goto skip;

	NutPunch_Log("New client is in...\n");
	perfectLobby->trailers[punch].heartbeat = NUTPUNCH_HEARTBEAT_RATE;
	memcpy(perfectLobby->players[punch].addr, &clientAddr.sin_addr, 4);
	perfectLobby->players[punch].port = clientAddr.sin_port;

skip:
	NutPunch_Serve_UpdateLobbies();
}

#endif

#endif
