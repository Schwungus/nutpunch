#pragma once

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
#define NUTPUNCH_SERVER_PORT (24869)

/// The maximum length of a lobby identifier excluding the null terminator. Not customizable.
#define NUTPUNCH_ID_MAX (64)

#ifdef NUTPUNCH_WINDOSE

#define WINVER 0x0601
#define _WIN32_WINNT 0x0601
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
const struct NutPunch* NutPunch_GetPeers();

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
		uint16_t portNE = (uint16_t)(*ptr++) << 8;
		portNE |= (uint16_t)(*ptr++);
#ifdef __BIG_ENDIAN__
		portNE = (portNE >> 8) | (portNE << 8);
#endif
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

const struct NutPunch* NutPunch_GetPeers() {
	return NutPunch_List;
}

int NutPunch_GetPeerCount() {
	return NutPunch_LastStatus == NP_Status_Error ? 0 : NutPunch_Count;
}

// The integrated hole-punch server:
#ifdef NUTPUNCH_COMPILE_SERVER

#define NUTPUNCH_LOBBY_MAX (16)
#define NUTPUNCH_HEARTBEAT_RATE (30)
#define NUTPUNCH_HEARTBEAT_REFILL (60)

struct NutPunch_Trailer {
	int heartbeat, prevHeartbeat;
	struct sockaddr addr;
};

struct NutPunch_Lobby {
	char identifier[NUTPUNCH_ID_MAX + 1];
	struct NutPunch players[NUTPUNCH_MAX_PLAYERS];
	struct NutPunch_Trailer trailers[NUTPUNCH_MAX_PLAYERS];
};

static struct NutPunch_Lobby lobbies[NUTPUNCH_LOBBY_MAX];

static void NutPunch_Server_Init() {
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

static void NutPunch_Server_KillLobby(struct NutPunch_Lobby* lobby) {
	if (lobby->identifier[0])
		NutPunch_Log("Destroying lobby '%s'", lobby->identifier);
	memset(lobby->players, 0, sizeof(*lobby->players) * NUTPUNCH_MAX_PLAYERS);
	memset(lobby->trailers, 0, sizeof(*lobby->trailers) * NUTPUNCH_MAX_PLAYERS);
	memset(lobby->identifier, 0, sizeof(lobby->identifier));
}

static void NutPunch_Server_Reset() {
	NutPunch_Log("Nuking lobby list");
	for (int i = 0; i < NUTPUNCH_LOBBY_MAX; i++)
		NutPunch_Server_KillLobby(&lobbies[i]);
}

static void NutPunch_WritePayload(const struct NutPunch_Lobby* lobby, int player, uint8_t** ptr) {
	if (lobby->trailers[player].heartbeat) {
		memcpy(*ptr, lobby->players[player].addr, 4);
		*ptr += 4;

		uint16_t portLE = lobby->players[player].port;
#ifdef __BIG_ENDIAN__
		portLE = (portLE >> 8) | (portLE << 8);
#endif
		*(*ptr)++ = portLE >> 8;
		*(*ptr)++ = portLE & 0xFF;
	} else {
		memset(*ptr, 0, 6);
		*ptr += 6;
	}
}

static void NutPunch_Server_UpdateLobby(struct NutPunch_Lobby* lobby) {
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		if (!lobby->trailers[i].heartbeat)
			continue;
		lobby->trailers[i].heartbeat -= 1;

		struct sockaddr* clientAddr = &lobby->trailers[i].addr;
		int addrLen = sizeof(*clientAddr);

		static char id[NUTPUNCH_ID_MAX + 1] = {0};
		memset(id, 0, sizeof(id));
		int64_t nRecv = recvfrom(NutPunch_LocalSocket, id, NUTPUNCH_ID_MAX, 0, clientAddr, &addrLen);

		if (nRecv == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK && WSAGetLastError() != WSAECONNRESET)
		{
			NutPunch_Log("Peer %d disconnect (code %d)", i + 1, WSAGetLastError());
			lobby->trailers[i].heartbeat = 0;
			continue;
		}
		if (nRecv != NUTPUNCH_ID_MAX)
			continue;
		if (memcmp(lobby->identifier, id, NUTPUNCH_ID_MAX)) {
			NutPunch_Log("Peer %d changed its lobby ID, which is currently unsupported", i + 1);
			lobby->trailers[i].heartbeat = 0;
			continue;
		}

		lobby->trailers[i].heartbeat = NUTPUNCH_HEARTBEAT_REFILL;
	}

	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		struct NutPunch_Trailer* tr = &lobby->trailers[i];
		if (!tr->heartbeat && tr->prevHeartbeat) {
			NutPunch_Log("Peer %d timed out", i + 1);
			memset(&lobby->players[i], 0, sizeof(struct NutPunch));
			memset(&lobby->trailers[i], 0, sizeof(struct NutPunch_Trailer));
		}
		tr->prevHeartbeat = tr->heartbeat;
	}

	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		if (lobby->trailers[i].heartbeat)
			goto announce;

	NutPunch_Server_KillLobby(lobby);
	return;

announce:
	for (int recpt = 0; recpt < NUTPUNCH_MAX_PLAYERS; recpt++) {
		if (!lobby->trailers[recpt].heartbeat)
			continue;

		static char payload[NUTPUNCH_PAYLOAD_SIZE] = {0};
		memset(payload, 0, sizeof(payload));

		uint8_t* ptr = (uint8_t*)payload;
		NutPunch_WritePayload(lobby, recpt, &ptr);

		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			if (i != recpt)
				NutPunch_WritePayload(lobby, i, &ptr);

		struct sockaddr addr = lobby->trailers[recpt].addr;
		int sent = sendto(NutPunch_LocalSocket, payload, sizeof(payload), 0, &addr, sizeof(addr));
		if (sent != SOCKET_ERROR || WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAECONNRESET)
			continue;

		lobby->trailers[recpt].heartbeat = 0;
		NutPunch_LastErrorCode = WSAGetLastError();
		NutPunch_LastError = "Peer connection lost";
		NutPunch_PrintError();
	}
}

static bool NutPunch_Server_LobbyEmpty(const struct NutPunch_Lobby* lobby) {
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		if (lobby->trailers[i].heartbeat > 0)
			return false;
	return true;
}

/// Update the hole-punch server. Needs to be run in a loop, preferrably at 60Hz; wastes CPU cycles otherwise.
void NutPunch_Serve() {
	if (NutPunch_LocalSocket == INVALID_SOCKET) {
		NutPunch_Server_Reset();
		NutPunch_Server_Init();
	}

	if (!NutPunch_MayAccept())
		goto done;

	static char recvBuf[NUTPUNCH_ID_MAX + 1] = {0};
	memset(recvBuf, 0, sizeof(recvBuf));

	struct sockaddr_in clientAddr = {0};
	int addrLen = sizeof(clientAddr);
	memset(&clientAddr, 0, addrLen);

	int nRecv
		= recvfrom(NutPunch_LocalSocket, recvBuf, NUTPUNCH_ID_MAX, 0, (struct sockaddr*)&clientAddr, &addrLen);
	if (nRecv == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK && WSAGetLastError() != WSAECONNRESET) {
		NutPunch_LastErrorCode = WSAGetLastError();
		NutPunch_LastError = "Socket recv error";
		NutPunch_PrintError();
		goto done;
	}
	if (nRecv != NUTPUNCH_ID_MAX) // skip weirdass packets
		goto done;

	int player = NUTPUNCH_LOBBY_MAX;
	struct NutPunch_Lobby* lobby = NULL;

	// Find lobby by supplied ID.
	for (int i = 0; i < NUTPUNCH_LOBBY_MAX; i++) {
		lobby = &lobbies[i];
		if (!memcmp(lobbies[i].identifier, recvBuf, NUTPUNCH_ID_MAX))
			goto checkPlayer;
	}

	// No such lobby; create it.
	for (int i = 0; i < NUTPUNCH_LOBBY_MAX; i++) {
		lobby = &lobbies[i];
		if (!NutPunch_Server_LobbyEmpty(lobby))
			continue;
		memcpy(lobby->identifier, recvBuf, NUTPUNCH_ID_MAX);
		NutPunch_Log("Lobby '%s' created!", recvBuf);
		player = 0;
		goto addPlayer;
	}

	NutPunch_Server_Reset(); // NUKE: we ran out of lobby slots
	goto done;

checkPlayer:
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
		if (!memcmp(&lobby->players[i].addr, &clientAddr.sin_addr, 4)
			&& lobby->players[i].port == clientAddr.sin_port)
		{
			goto done; // found the mfer
		}
	for (player = 0; player < NUTPUNCH_MAX_PLAYERS; player++) {
		if (!lobby->trailers[player].heartbeat)
			goto addPlayer; // i'm replacing your bitch ass
	}
	goto done; // ran out of player slots........

addPlayer:
	NutPunch_Log("New peer is in...");

	memcpy(&lobby->players[player].addr, &clientAddr.sin_addr, 4);
	lobby->players[player].port = clientAddr.sin_port;

	memcpy(&lobby->trailers[player].addr, &clientAddr, sizeof(struct sockaddr));
	lobby->trailers[player].heartbeat = NUTPUNCH_HEARTBEAT_REFILL + 1;
	lobby->trailers[player].prevHeartbeat = NUTPUNCH_HEARTBEAT_REFILL + 2;

done:
	for (int i = 0; i < NUTPUNCH_LOBBY_MAX; i++)
		NutPunch_Server_UpdateLobby(&lobbies[i]);
}

#endif

#endif
