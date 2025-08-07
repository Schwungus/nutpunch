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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(NUTPUNCH_IMPLEMENTATION) && !defined(TINYCSOCKET_IMPLEMENTATION)
#define TINYCSOCKET_IMPLEMENTATION
#endif

#include "tinycsocket.h"

#if defined(NUTPUNCH_IMPLEMENTATION) && defined(NUTPUNCH_WINDOSE)
#include <windows.h>
#include <winsock2.h>
#endif

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

static TcsSocket NutPunch_LocalSock;
static struct TcsAddress NutPunch_RemoteAddr = {0}, NutPunch_PuncherServer = {0};

static void NutPunch_LazyInit() {
	if (!NutPunch_HadInit) {
		tcs_lib_init();
		srand(time(NULL));
		NutPunch_LocalSock = TCS_NULLSOCKET;
		NutPunch_HadInit = true;
	}
}

uint16_t NutPunch_GeneratePort() {
	NutPunch_LazyInit();
	return NUTPUNCH_MIN_PORT + rand() % (NUTPUNCH_MAX_PORT - NUTPUNCH_MIN_PORT + 1);
}

void NutPunch_SetServerAddr(const char* addr) {
	if (addr == NULL)
		goto null;

	static char buf[128] = {0};
	snprintf(buf, sizeof(buf), "%s:%d", addr, NUTPUNCH_SERVER_PORT);
	if (tcs_util_string_to_address(buf, &NutPunch_PuncherServer) == TCS_SUCCESS)
		return;

null:
	memset(&NutPunch_PuncherServer, 0, sizeof(NutPunch_PuncherServer));
}

static void NutPunch_PrintError() {
	if (!NutPunch_LastErrorCode)
		NutPunch_Log("WARN: %s\n", NutPunch_LastError);
	else
		NutPunch_Log("WARN: %s (error code: %d)\n", NutPunch_LastError, NutPunch_LastErrorCode);
}

static bool NutPunch_BindSocket(uint16_t port) {
	NutPunch_LocalSock = TCS_NULLSOCKET;

	NutPunch_LastErrorCode = tcs_create(&NutPunch_LocalSock, TCS_TYPE_UDP_IP4);
	if (NutPunch_LastErrorCode != TCS_SUCCESS) {
		NutPunch_LocalSock = TCS_NULLSOCKET;
		NutPunch_LastError = "Failed to create the underlying UDP socket";
		goto fail;
	}

	NutPunch_LastErrorCode = tcs_set_receive_timeout(NutPunch_LocalSock, 0);
	if (NutPunch_LastErrorCode != TCS_SUCCESS) {
		NutPunch_LastError = "Failed to set receive timeout";
		goto failSock;
	}

	NutPunch_LastErrorCode = tcs_bind(NutPunch_LocalSock, port);
	if (NutPunch_LastErrorCode != TCS_SUCCESS) {
		NutPunch_LastError = "Failed to bind the underlying UDP socket";
		goto failSock;
	}

	return true;

failSock:
	tcs_destroy(&NutPunch_LocalSock);
fail:
	NutPunch_LastStatus = NP_Status_Error;
	NutPunch_PrintError();
	return false;
}

static bool NutPunch_GotData() {
#ifdef NUTPUNCH_WINDOSE
	struct timeval instantBitchNoodles = {0};
	fd_set s = {1, {NutPunch_LocalSock}};
	NutPunch_LastErrorCode = select(0, &s, NULL, NULL, &instantBitchNoodles);

	if (NutPunch_LastErrorCode < 0) {
		NutPunch_LastErrorCode = WSAGetLastError();
		NutPunch_LastError = "Socket poll failed";
		NutPunch_PrintError();

		tcs_destroy(&NutPunch_LocalSock);
		return 0;
	}

	return NutPunch_LastErrorCode > 0;
#else
#error Bad luck...
#endif
}

void NutPunch_Join(const char* lobby) {
	NutPunch_LazyInit();

	if (!*(uint8_t*)&NutPunch_PuncherServer) {
		NutPunch_LastError = "Holepuncher server address unset";
		NutPunch_LastErrorCode = 0;
		NutPunch_PrintError();

		NutPunch_LastStatus = NP_Status_Error;
		return;
	}

	if (NutPunch_BindSocket(NutPunch_GeneratePort())) {
		snprintf((char*)NutPunch_LobbyId, sizeof(NutPunch_LobbyId), "%s", lobby);
		NutPunch_RemoteAddr = NutPunch_PuncherServer;
		NutPunch_LastStatus = NP_Status_Idle;
	} else
		NutPunch_LastStatus = NP_Status_Error;
}

void NutPunch_Cleanup() {
	if (NutPunch_LocalSock != TCS_NULLSOCKET)
		tcs_destroy(&NutPunch_LocalSock);
	tcs_lib_free();
}

const char* NutPunch_GetLastError() {
	return NutPunch_LastError;
}

static int NutPunch_QueryImpl() {
	NutPunch_Count = 0;

	if (!NutPunch_LobbyId[0] || NutPunch_LocalSock == TCS_NULLSOCKET)
		return NP_Status_Idle;
	if (!*(uint8_t*)&NutPunch_PuncherServer) {
		NutPunch_LastErrorCode = 0;
		NutPunch_LastError = "Holepuncher server address unset";
		goto fail;
	}

	static uint64_t queryCount = 0, nRecv = 0;
	if (!(queryCount++ % NUTPUNCH_SEND_INTERVAL)) {
		NutPunch_LastErrorCode =
		    tcs_send_to(NutPunch_LocalSock, NutPunch_LobbyId, NUTPUNCH_ID_MAX, 0, &NutPunch_RemoteAddr, &nRecv);
		if (NutPunch_LastErrorCode != TCS_SUCCESS) {
			queryCount = 0;
			NutPunch_LastError = "Failed to send lobby ID";
			goto fail;
		}
	}

	if (!NutPunch_GotData())
		return NP_Status_InProgress;
	uint8_t buf[NUTPUNCH_PAYLOAD_SIZE] = {0}, *ptr = buf;
	NutPunch_LastErrorCode =
	    tcs_receive_from(NutPunch_LocalSock, buf, sizeof(buf), 0, &NutPunch_RemoteAddr, &nRecv);
	if (NutPunch_LastErrorCode != TCS_SUCCESS) {
		NutPunch_LastError = "Failed to receive from holepunch server";
		goto fail;
	}
	if (!nRecv)
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
	if (NutPunch_LocalSock != TCS_NULLSOCKET)
		tcs_destroy(&NutPunch_LocalSock);
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
	struct TcsAddress tcsAddr;
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

static void NutPunch_Serve_UpdateLobbies() {
	static uint8_t recvBuf[NUTPUNCH_ID_MAX + 1] = {0};

	for (struct NutPunch_Lobby* lobby = lobbies; lobby < lobbies + NUTPUNCH_LOBBY_MAX; lobby++) {
		for (size_t i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
			if (lobby->trailers[i].heartbeat > 0)
				lobby->trailers[i].heartbeat -= 1;
			if (!NutPunch_GotData())
				continue;

			size_t nRecv = 0;
			memset(recvBuf, 0, sizeof(recvBuf));
			NutPunch_LastErrorCode = tcs_receive_from(
			    NutPunch_LocalSock, recvBuf, NUTPUNCH_ID_MAX, 0, &lobby->trailers[i].tcsAddr, &nRecv
			);

			if (NutPunch_LastErrorCode == TCS_SUCCESS) {
				if (nRecv)
					lobby->trailers[i].heartbeat = NUTPUNCH_HEARTBEAT_RATE;
			} else
				lobby->trailers[i].heartbeat = 0; // just kill him.....
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

			size_t junk = 0;
			NutPunch_LastErrorCode = tcs_send_to(
			    NutPunch_LocalSock, packet, sizeof(packet), 0, &lobby->trailers[recptIdx].tcsAddr, &junk
			);

			if (NutPunch_LastErrorCode != TCS_SUCCESS)
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
	if (!NutPunch_HadInit) {
		NutPunch_Serve_Reset();
		NutPunch_LazyInit();
		NutPunch_Serve_Init();
	}

	struct TcsAddress clientAddr = {0};
	static uint8_t buf[NUTPUNCH_ID_MAX + 1] = {0};
	memset(buf, 0, sizeof(buf));
	size_t nRecv = 0;

	if (!NutPunch_GotData())
		goto skip;

	NutPunch_LastErrorCode = tcs_receive_from(NutPunch_LocalSock, buf, NUTPUNCH_ID_MAX, 0, &clientAddr, &nRecv);
	if (NutPunch_LastErrorCode != TCS_SUCCESS) {
		NutPunch_LastError = "Client socket recv error";
		NutPunch_PrintError();
		goto skip;
	}
	if (!nRecv)
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
		uint32_t fuckyHost = 0;
		const uint8_t* cp = perfectLobby->players[i].addr;
		tcs_util_ipv4_args(cp[0], cp[1], cp[2], cp[3], &fuckyHost);

		bool sameHost = !memcmp(&fuckyHost, &clientAddr.data.af_inet.address, 4);
		bool samePort = clientAddr.data.af_inet.port == perfectLobby->players[i].port;

		if (sameHost && samePort)
			goto skip; // already in
	}
	for (size_t i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
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
	perfectLobby->trailers[punch].tcsAddr = clientAddr;
	memcpy(perfectLobby->players[punch].addr, &clientAddr.data.af_inet.address, 4);
	perfectLobby->players[punch].port = clientAddr.data.af_inet.port;
skip:
	NutPunch_Serve_UpdateLobbies();
}

#endif

#endif
