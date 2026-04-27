// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.
//
// In jurisdictions that recognize copyright laws, the author or authors
// of this software dedicate any and all copyright interest in the
// software to the public domain. We make this dedication for the benefit
// of the public at large and to the detriment of our heirs and
// successors. We intend this dedication to be an overt act of
// relinquishment in perpetuity of all present and future rights to this
// software under copyright law.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//
// For more information, please refer to <https://unlicense.org>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "NutPunch.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    NutPunch_Field* metadata;
    NP_SockAddr address;
    NutPunch_Clock last_ping;
} NP_PeerInfo;

typedef struct {
    NP_SockAddr from;
    const uint8_t* data;
    size_t len;
} NP_Message;

typedef struct NP_IncomingData {
    NutPunch_Peer peer;
    void* data;
    size_t len;
    struct NP_IncomingData* next;
} NP_IncomingData;

typedef struct NP_OutgoingPacket {
    NP_SockAddr destination;
    void* data;
    struct NP_OutgoingPacket* next;
    int len, retries;
    NutPunch_Clock last_retry;
    bool acked;
    uint32_t id;
} NP_OutgoingPacket;

typedef struct {
    const char identifier[sizeof(NP_Header) + 1];
    void (*const handle)(NP_Message);
    const int64_t min_packet_size;
} NP_MessageType;

static void NP_HandlePing(NP_Message), NP_HandleGTFO(NP_Message), NP_HandleBeating(NP_Message),
    NP_HandleListing(NP_Message), NP_HandleLobbyMetadata(NP_Message), NP_HandleData(NP_Message),
    NP_HandleQueue(NP_Message), NP_HandleDate(NP_Message), NP_HandleAcky(NP_Message);

static const NP_MessageType NP_MessageTypes[] = {
    {"ACKY", NP_HandleAcky,          4                                             },
    {"PING", NP_HandlePing,          1                                             },
    {"LIST", NP_HandleListing,       0                                             },
    {"LGMA", NP_HandleLobbyMetadata, sizeof(NutPunch_LobbyId) + sizeof(NP_Metadata)},
    {"DATA", NP_HandleData,          1                                             },
    {"GTFO", NP_HandleGTFO,          1                                             },
    {"BEAT", NP_HandleBeating,       sizeof(NP_Beating)                            },
    {"QUEU", NP_HandleQueue,         1 + 1                                         },
    {"DATE", NP_HandleDate,          sizeof(NutPunch_LobbyId)                      },
};

void NP_DefaultLogger(const char* fmt, ...) {
    va_list args = {0};
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);

    fflush(stdout);
}

void (*NP_Logger)(const char*, ...) = NULL;
char NP_LastError[512] = "";

static bool NP_InitDone = false, NP_Closing = false;
static NutPunch_UpdateStatus NP_LastStatus = NPS_Idle;

static NP_Sock NP_Socket = NUTPUNCH_INVALID_SOCKET;
static NutPunch_Clock NP_LastBeating = 0;

static char NP_LobbyId[sizeof(NutPunch_LobbyId) + 1] = {0};
static NutPunch_PeerId NP_PeerId = {0};
static NutPunch_QueueId NP_QueueId = {0};

static NP_PeerInfo NP_Peers[NUTPUNCH_MAX_PLAYERS] = {0};
static NutPunch_Peer NP_LocalPeer = NUTPUNCH_MAX_PLAYERS, NP_Master = NUTPUNCH_MAX_PLAYERS,
                     NP_MaxPlayers = 0;

static NutPunch_Callback NP_Callbacks[NPCB_Count] = {0};

static NP_SockAddr NP_ServerAddr = {0};
static char NP_ServerHost[128] = {0};

static NutPunch_Channel NP_ChannelCount = 1;
static NP_IncomingData* NP_Unread[NUTPUNCH_MAX_CHANNELS] = {0};
static NP_OutgoingPacket* NP_Pending = NULL;

static bool NP_Unlisted = false;

static NP_NetMode NP_Mode = NPNM_Normal;
static NP_HeartbeatFlagsStorage NP_HeartbeatFlags = 0;
static int NP_QueueCount = 0, NP_QueueTime = 0;

static NutPunch_Field *NP_LobbyMetadata = NULL, *NP_PeerMetadata = NULL;

static void NP_JustSend(NP_SockAddr destination, const void* data, size_t len, bool reliable) {
    const int prefix = 4;

    if (len > NUTPUNCH_FRAGMENT_SIZE)
        return;

    static uint32_t counter = 0;

    NP_OutgoingPacket* last = NP_Pending;
    for (; last && last->next; last = last->next) {}

    last = *(last ? &last->next : &NP_Pending) = NutPunch_Malloc(sizeof(*last));
    last->destination = destination, last->next = NULL;
    last->retries = reliable ? 0 : -1, last->last_retry = 0, last->acked = false;

    // prefixing the entire packet with an id...
    last->id = reliable ? counter++ : 0;
    last->len = prefix + (int)len, last->data = NutPunch_Malloc(prefix + len);
    NutPunch_Memcpy(last->data + prefix, data, len);
    *(uint32_t*)last->data = htonl(last->id);
}

void NP_NukeSocket(NP_Sock* sock) {
    if (*sock == NUTPUNCH_INVALID_SOCKET)
        return;
#ifdef NUTPUNCH_WINDOSE
    shutdown(*sock, SD_BOTH);
    closesocket(*sock);
#else
    close(*sock);
#endif
    *sock = NUTPUNCH_INVALID_SOCKET;
}

static void NP_NukeLobbyDataLite() {
    NP_Closing = NP_Unlisted = false;
    NP_LocalPeer = NP_Master = NUTPUNCH_MAX_PLAYERS;
    NP_Memzero(NP_Peers);

    NP_Mode = NPNM_Normal;
    NP_HeartbeatFlags = NP_QueueCount = NP_QueueTime = 0;
    NP_LobbyId[0] = 0;

    NutPunch_SetMaxPlayers(NUTPUNCH_MAX_PLAYERS);

    for (size_t i = 0; i < NUTPUNCH_MAX_CHANNELS; i++) {
        while (NP_Unread[i]) {
            NP_IncomingData* ptr = NP_Unread[i];
            NP_Unread[i] = ptr->next;
            NutPunch_Free(ptr->data), NutPunch_Free(ptr);
        }
    }

    while (NP_Pending) {
        NP_OutgoingPacket* ptr = NP_Pending;
        NP_Pending = ptr->next;
        NutPunch_Free(ptr->data), NutPunch_Free(ptr);
    }

    NP_NukeSocket(&NP_Socket);
}

static void NP_NukeMetadata(NutPunch_Field** metadata) {
    while (metadata && *metadata) {
        NutPunch_Field* ptr = *metadata;
        *metadata = ptr->next;
        NutPunch_Free(ptr);
    }
}

static void NP_NukeLobbyData() {
    NP_NukeLobbyDataLite();
    NP_NukeMetadata(&NP_LobbyMetadata);
    NP_NukeMetadata(&NP_PeerMetadata);
}

static void NP_ResetImpl() {
    NP_NukeLobbyData();

    NP_MemzeroRef(NP_ServerAddr), NP_Memzero(NP_Peers);
    NP_LastStatus = NPS_Idle;
}

static void NP_LazyInit() {
    if (NP_InitDone)
        return;
    NP_InitDone = true;

#ifdef NUTPUNCH_WINDOSE
    WSADATA bitch = {0};
    WSAStartup(MAKEWORD(2, 2), &bitch);
#endif

    srand(NutPunch_TimeNS());
    for (size_t i = 0; i < sizeof(NutPunch_PeerId); i++)
        NP_PeerId[i] = (char)('A' + rand() % 26);

    NP_ResetImpl();

    void (*const print)(const char*, ...) = (NP_Logger ? NP_Logger : NP_DefaultLogger);
    print(".-------------------------------------------------------------.\n");
    print("| For troubleshooting multiplayer connectivity, please visit: |\n");
    print("|    https://github.com/Schwungus/nutpunch#troubleshooting    |\n");
    print("'-------------------------------------------------------------'\n");
}

void NutPunch_Shutdown() {
    NutPunch_Disconnect();

#ifdef NUTPUNCH_WINDOSE
    WSACleanup();
#endif
}

void NutPunch_Reset() {
    NP_LazyInit();
    NP_ResetImpl();
}

void NutPunch_SetServerAddr(const char* hostname) {
    if (!hostname)
        hostname = "";
    NutPunch_SNPrintF(NP_ServerHost, sizeof(NP_ServerHost), "%s", hostname);
}

static int NP_FieldNameSize(const char* name) {
    if (!name)
        return 0;
    for (int i = 0; i < NUTPUNCH_FIELD_NAME_MAX; i++)
        if (!name[i])
            return i;
    return NUTPUNCH_FIELD_NAME_MAX;
}

static NutPunch_Field* NP_GetPeerFields(NutPunch_Peer peer) {
    if (NutPunch_IsOnline() && peer == NutPunch_LocalPeer())
        return NP_PeerMetadata;
    else if (peer >= 0 && peer < NUTPUNCH_MAX_PLAYERS)
        return NP_Peers[peer].metadata;
    else
        return NULL;
}

static size_t NP_GetVarCount(const NutPunch_Field* fields) {
    size_t count = 0;
    while (fields)
        count++, fields = fields->next;
    return count;
}

static const char* NP_GetVar(const NutPunch_Field* fields, const char* name) {
    if (!fields || !name)
        return NULL;

    for (const NutPunch_Field* ptr = fields; ptr; ptr = ptr->next)
        if (!NutPunch_StrNCmp(name, ptr->name, NUTPUNCH_FIELD_NAME_MAX))
            return ptr->data;

    return NULL;
}

static void NP_SetVar(NutPunch_Field** fields, const char* name, const char* data) {
    if (!fields || !name || !data)
        return;

    NutPunch_Field* target = *fields;

    for (; target; target = target->next)
        if (!NutPunch_StrNCmp(target->name, name, NUTPUNCH_FIELD_NAME_MAX))
            break;

    if (!target) {
        if (NP_GetVarCount(*fields) + 1 > NUTPUNCH_MAX_FIELDS) {
            NP_Warn("Can't add more than %d fields!", NUTPUNCH_MAX_FIELDS);
            return;
        }

        target = NutPunch_Malloc(sizeof(*target));
        NP_MemzeroRef(*target);

        NutPunch_SNPrintF(target->name, sizeof(target->name), "%s", name);
        target->next = *fields, *fields = target;
    }

    NutPunch_SNPrintF(target->data, sizeof(target->data), "%s", data);
}

static int NP_DumpMetadata(void* raw_out, const NutPunch_Field* fields) {
    char* out = raw_out;
    for (; fields; fields = fields->next) {
        NutPunch_Memcpy(out, fields, sizeof(NP_Field));
        out += sizeof(NP_Field);
    }

    return (int)(out - (char*)raw_out);
}

void NutPunch_RequestLobbyData(const NutPunch_LobbyId lobby) {
    if (NP_Socket == NUTPUNCH_INVALID_SOCKET)
        return;

    static uint8_t buf[sizeof(NP_Header) + sizeof(NutPunch_LobbyId)] = "LIST";
    NutPunch_Memcpy(buf + sizeof(NP_Header), lobby, sizeof(NutPunch_LobbyId));

    NP_JustSend(NP_ServerAddr, buf, sizeof(buf), false);
}

const char* NutPunch_GetLobbyData(const char* name) {
    return NP_GetVar(NP_LobbyMetadata, name);
}

const char* NutPunch_GetPeerData(NutPunch_Peer peer, const char* name) {
    return NP_GetVar(NP_GetPeerFields(peer), name);
}

void NutPunch_SetLobbyData(const char* name, const char* data) {
    NP_SetVar(&NP_LobbyMetadata, name, data);
}

void NutPunch_SetPeerData(const char* name, const char* data) {
    NP_SetVar(&NP_PeerMetadata, name, data);
}

static bool NP_ResolveNutpuncher() {
    NP_LazyInit();

    struct addrinfo *resolved = NULL, hints = {0};
    hints.ai_family = AF_INET, hints.ai_socktype = SOCK_DGRAM, hints.ai_protocol = IPPROTO_UDP;

    if (!NP_ServerHost[0]) {
        NutPunch_SetServerAddr(NUTPUNCH_DEFAULT_SERVER);
        NP_Info("Connecting to the public NutPuncher as none was explicitly specified");
    }

    static char portfmt[8] = {0};
    NutPunch_SNPrintF(portfmt, sizeof(portfmt), "%d", NUTPUNCH_SERVER_PORT);

    if (getaddrinfo(NP_ServerHost, portfmt, &hints, &resolved)) {
        NP_Warn("NutPuncher server address failed to resolve");
        goto fail;
    }

    if (!resolved) {
        NP_Warn("Couldn't resolve NutPuncher address");
        goto fail;
    }

    NP_MemzeroRef(NP_ServerAddr);
    NutPunch_Memcpy(&NP_ServerAddr, resolved->ai_addr, resolved->ai_addrlen);
    freeaddrinfo(resolved);

    NP_Info("Resolved NutPuncher address");
    return true;

fail:
    NP_LastStatus = NPS_Error;
    return false;
}

bool NP_MakeNonblocking(NP_Sock sock) {
#ifdef NUTPUNCH_WINDOSE
    u_long argp = 1;
    return !ioctlsocket(sock, FIONBIO, &argp);
#else
    return !fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
#endif
}

bool NP_MakeReuseAddr(NP_Sock sock) {
#ifdef NUTPUNCH_WINDOSE
    const u_long argp = 1;
#else
    const uint32_t argp = 1;
#endif
    const char* const shit = (const char*)&argp;
    return !setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, shit, sizeof(argp));
}

static bool NP_BindSocket() {
    NP_SockAddr local = {0};
    NP_LazyInit(), NP_NukeSocket(&NP_Socket);
    NP_Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (NP_Socket == NUTPUNCH_INVALID_SOCKET) {
        const int err = NP_SockError();
        NP_Warn("Failed to create the underlying UDP socket (%d)", err);
        goto sockfail;
    }

    if (!NP_MakeReuseAddr(NP_Socket)) {
        const int err = NP_SockError();
        NP_Warn("Failed to set socket reuseaddr option (%d)", err);
        goto sockfail;
    }

    if (!NP_MakeNonblocking(NP_Socket)) {
        const int err = NP_SockError();
        NP_Warn("Failed to set socket to non-blocking mode (%d)", err);
        goto sockfail;
    }

    local.sin_family = AF_INET;
    local.sin_port = htons(0);
    local.sin_addr.s_addr = htonl(INADDR_ANY);

    if (!bind(NP_Socket, (struct sockaddr*)&local, sizeof(local)))
        return true;

    NP_Warn("Failed to bind a UDP socket (%d)", NP_SockError());
sockfail:
    NutPunch_Reset();
    return false;
}

static bool NP_Connect(const char* lobby_id, bool sane, NP_HeartbeatFlagsStorage flags) {
    NP_LazyInit();

    if (flags & NP_HB_Queue)
        NP_NukeLobbyDataLite();
    else
        NP_NukeLobbyData();

    if (sane && (!lobby_id || !lobby_id[0])) {
        NP_Warn("Lobby ID cannot be null or empty!");
        NP_LastStatus = NPS_Error;
        return false;
    }

    if (!NP_BindSocket() || !NP_ResolveNutpuncher())
        return false;

    NP_HeartbeatFlags = flags;

    NP_Info("Ready to send heartbeats");
    NP_LastStatus = NPS_Online;
    NP_Memzero(NP_LastError);

    if (lobby_id)
        NutPunch_SNPrintF(NP_LobbyId, sizeof(NP_LobbyId), "%s", lobby_id);

    NP_LastBeating = NutPunch_TimeNS();

    return true;
}

bool NutPunch_QueryMode() {
    if (!NP_Connect(NULL, false, 0))
        return false;
    NP_Mode = NPNM_Query;
    return true;
}

bool NutPunch_Host(const char* lobby_id) {
    if (!NP_Connect(lobby_id, true, 0))
        return false;

    NP_Mode = NPNM_Normal;
    NP_Info("Hosting lobby '%s'", NP_LobbyId);

    return true;
}

bool NutPunch_Join(const char* lobby_id) {
    if (!NP_Connect(lobby_id, true, NP_HB_JoinExisting))
        return false;

    NP_Mode = NPNM_Normal;
    NP_Info("Joining lobby '%s'", NP_LobbyId);

    return true;
}

bool NutPunch_EnterQueue(const char* queue_id) {
    NP_LazyInit();

    if (!NP_Connect(NULL, false, 0))
        return false;

    NP_Mode = NPNM_Matchmaking;
    NP_Memzero(NP_QueueId);

    if (queue_id)
        NutPunch_SNPrintF(NP_QueueId, sizeof(NP_QueueId), "%s", queue_id);

    return true;
}

int NutPunch_QueueTime() {
    return NP_QueueTime;
}

int NutPunch_QueueCount() {
    return NP_QueueCount;
}

void NutPunch_SetUnlisted(bool unlisted) {
    if (unlisted)
        NP_HeartbeatFlags |= NP_HB_Unlisted;
    else
        NP_HeartbeatFlags &= ~NP_HB_Unlisted;
}

bool NutPunch_IsUnlisted() {
    return NP_Unlisted;
}

void NutPunch_SetMaxPlayers(int players) {
    if (players < 2 || players > NUTPUNCH_MAX_PLAYERS) {
        NP_Warn("Setting %d players max (requested %d)", NUTPUNCH_MAX_PLAYERS, players);
        players = NUTPUNCH_MAX_PLAYERS;
    }

    NP_HeartbeatFlags &= 0xF;
    NP_HeartbeatFlags |= (players - 1) << 4;
}

int NutPunch_GetMaxPlayers() {
    if (!NutPunch_IsOnline() || NP_MaxPlayers > NUTPUNCH_MAX_PLAYERS)
        return 0;
    return NP_MaxPlayers;
}

void NutPunch_FindLobbies(int filter_count, const NutPunch_Filter* filters) {
    if (NP_Socket == NUTPUNCH_INVALID_SOCKET)
        return;

    if (filters != NULL && filter_count > NUTPUNCH_MAX_SEARCH_FILTERS) {
        NP_Warn("Filter count exceeded in `NutPunch_FindLobbies`; truncating the input");
        filter_count = NUTPUNCH_MAX_SEARCH_FILTERS;
    }

    static uint8_t query[sizeof(NP_Header) + NUTPUNCH_MAX_SEARCH_FILTERS * sizeof(NutPunch_Filter)]
        = "LIST";
    uint8_t* ptr = query + sizeof(NP_Header);

    if (filter_count > 0 && filters != NULL) {
        NutPunch_Memcpy(ptr, filters, filter_count * sizeof(NutPunch_Filter));
        ptr += filter_count * sizeof(NutPunch_Filter);
    }

    NP_JustSend(NP_ServerAddr, query, ptr - query, false);
}

const char* NutPunch_GetLastError() {
    return NP_LastError;
}

static void NP_HandleEventCb(NutPunch_CallbackEvent event, const void* data) {
    if (NP_Callbacks[event])
        NP_Callbacks[event](data);
}

static const char* NP_FormatSockaddr(NP_SockAddr addr) {
    static char buf[64] = "";

    const char* s = inet_ntoa(addr.sin_addr);
    NutPunch_SNPrintF(buf, sizeof(buf), "[%s]:%d", s, ntohs(addr.sin_port));

    return buf;
}

static void NP_KillPeer(NutPunch_Peer peer) {
    if (peer >= NUTPUNCH_MAX_PLAYERS)
        return;

    if (NutPunch_PeerAlive(peer))
        NP_HandleEventCb(NPCB_PeerLeft, &peer);

    NP_PeerInfo* ptr = &NP_Peers[peer];
    NP_NukeMetadata(&ptr->metadata);
    NP_MemzeroRef(*ptr);
}

bool NP_AddrNull(NP_SockAddr addr) {
    return !ntohl(addr.sin_addr.s_addr) && !ntohs(addr.sin_port);
}

bool NP_AddrEq(NP_SockAddr a, NP_SockAddr b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

static void NP_HandlePing(NP_Message msg) {
    const uint8_t* ptr = msg.data;

    const NutPunch_Peer idx = *ptr++;
    msg.len--;

    if (idx >= NUTPUNCH_MAX_PLAYERS)
        return;

    const bool was_dead = !NutPunch_PeerAlive(idx);

    NP_PeerInfo* const peer = &NP_Peers[idx];
    peer->address = msg.from, peer->last_ping = NutPunch_TimeNS();

    static NutPunch_PeerFieldDiff changed[NUTPUNCH_MAX_FIELDS] = {0};
    NP_Memzero(changed);

    size_t changed_count = 0;

    for (size_t i = 0; i < msg.len / sizeof(NP_Field); i++) {
        NP_Field now = ((NP_Field*)ptr)[i];

        for (NutPunch_Field* then = NP_LobbyMetadata; then; then = then->next) {
            if (NutPunch_StrNCmp(then->name, now.name, NUTPUNCH_FIELD_NAME_MAX))
                continue;

            if (!NutPunch_StrNCmp(then->data, now.data, NUTPUNCH_FIELD_DATA_MAX))
                continue;

            NutPunch_PeerFieldDiff* diff = &changed[changed_count++];
            NutPunch_SNPrintF(diff->name, sizeof(diff->name), "%s", then->name);
            NutPunch_SNPrintF(diff->then, sizeof(diff->then), "%s", then->data);
            NutPunch_SNPrintF(diff->now, sizeof(diff->now), "%s", now.data);
            diff->peer = idx;

            break;
        }

        NP_SetVar(&peer->metadata, now.name, now.data);
    }

    if (was_dead)
        NP_HandleEventCb(NPCB_PeerJoined, &idx);

    // makes more sense to emit peer metadata changes AFTER they've been joined.
    for (size_t i = 0; i < changed_count; i++)
        NP_HandleEventCb(NPCB_PeerMetadataChanged, &changed[i]);
}

static void NP_HandleGTFO(NP_Message msg) {
    if (!NP_AddrEq(msg.from, NP_ServerAddr))
        return;

    switch (msg.data[0]) {
    case NPE_NoSuchLobby:
        NP_Warn("Lobby doesn't exist: '%s'", NP_LobbyId);
        break;
    case NPE_LobbyExists:
        NP_Warn("The lobby you're hosting exists: '%s'", NP_LobbyId);
        break;
    case NPE_LobbyFull:
        NP_Warn("Lobby '%s' is full!", NP_LobbyId);
        break;
    case NPE_QueueNoMatch:
        NP_Warn("We found no players to match you with!");
        break;
    case NPE_Sybau:
        NP_Warn("sybau :wilted_rose:");
        break;
    default:
        NP_Warn("Unidentified error");
        break;
    }

    NP_LastStatus = NPS_Error;
}

static void NP_PrintOurAddress(const uint8_t* data) {
    NP_SockAddr addr = {0};
    addr.sin_addr.s_addr = *(uint32_t*)data, data += 4;
    addr.sin_port = ntohs(*(uint16_t*)data), data += 2;
    NP_Info("Server thinks you are %s", NP_FormatSockaddr(addr));
}

static NutPunch_Peer NP_FindPeer(NP_SockAddr addr) {
    for (NutPunch_Peer i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
        if (NP_AddrEq(NP_Peers[i].address, addr))
            return i;
    return NUTPUNCH_MAX_PLAYERS;
}

static void NP_SendPings(int idx, const uint8_t* data) {
    if (NP_Socket == NUTPUNCH_INVALID_SOCKET || idx == NP_LocalPeer)
        return;

    NP_SockAddr pub = {0}, same_nat = {0};

    if (data) {
        pub.sin_addr.s_addr = *(uint32_t*)data, data += 4;
        pub.sin_port = *(uint16_t*)data, data += 2;

        same_nat.sin_addr.s_addr = *(uint32_t*)data, data += 4;
        same_nat.sin_port = *(uint16_t*)data, data += 2;
    }

    if (NP_AddrNull(pub) && NP_AddrNull(same_nat)) { // they're dead on the NutPuncher's side
        NP_KillPeer(idx);
        return;
    }

    NP_PeerInfo* const peer = &NP_Peers[idx];

    static uint8_t ping[sizeof(NP_Header) + 1 + sizeof(NP_Metadata)] = "PING";
    uint8_t* ptr = &ping[sizeof(NP_Header)];

    *ptr++ = NP_LocalPeer;
    ptr += NP_DumpMetadata(ptr, NP_PeerMetadata);

    NP_JustSend(pub, ping, ptr - ping, false);
    NP_JustSend(same_nat, ping, ptr - ping, false);
}

static void NP_HandleBeating(NP_Message msg) {
    if (!NP_AddrEq(msg.from, NP_ServerAddr))
        return;

    const bool just_joined = NP_LocalPeer == NUTPUNCH_MAX_PLAYERS;
    const NutPunch_Peer old_master = NutPunch_MasterPeer();
    const uint8_t* ptr = msg.data;

    NP_Unlisted = *ptr++;
    NP_LocalPeer = *ptr++;
    NP_Master = *ptr++;
    const size_t num_peers = *ptr++;
    NP_MaxPlayers = *ptr++;

    if (NP_LocalPeer >= NUTPUNCH_MAX_PLAYERS) {
        NP_Warn("NutPuncher sent us a junk response?!");
        return;
    }

    const NutPunch_Peer new_master = NutPunch_MasterPeer();

    // add the join-existing flag after a successful join because otherwise we could get a
    // random-ass disconnection with the "Lobby already exists!" error message.
    NP_HeartbeatFlags |= NP_HB_JoinExisting;

    // sync outgoing max player count with the one we just received.
    NP_HeartbeatFlags &= 0xF;
    NP_HeartbeatFlags |= (NutPunch_GetMaxPlayers() - 1) << 4;

    const size_t expected_len = num_peers * (1 + 2 * sizeof(NP_PeerAddr));
    msg.len -= sizeof(NP_Beating);

    if (msg.len < expected_len) {
        NP_Warn("Bad peer data in beating");
        goto done_getting_beat;
    }

    static const uint8_t* addrs[NUTPUNCH_MAX_PLAYERS] = {0};
    NutPunch_Memset((void*)addrs, 0, sizeof(addrs));
    for (int i = 0; i < num_peers; i++) {
        const NutPunch_Peer pidx = *ptr++;
        addrs[pidx] = ptr;
        ptr += 2 * sizeof(NP_PeerAddr);
    }

    for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
        NP_SendPings(i, addrs[i]);
        if (i == NP_LocalPeer && just_joined && addrs[i])
            NP_PrintOurAddress(addrs[i]);
    }

    for (size_t i = 0; i < msg.len / sizeof(NP_Field); i++) {
        NP_Field now = ((NP_Field*)ptr)[i];

        for (NutPunch_Field* then = NP_LobbyMetadata; then; then = then->next) {
            if (NutPunch_StrNCmp(then->name, now.name, NUTPUNCH_FIELD_NAME_MAX))
                continue;

            if (NutPunch_StrNCmp(then->data, now.data, NUTPUNCH_FIELD_DATA_MAX))
                continue;

            NutPunch_FieldDiff diff = {0};
            NutPunch_SNPrintF(diff.name, sizeof(diff.name), "%s", then->name);
            NutPunch_SNPrintF(diff.then, sizeof(diff.then), "%s", then->data);
            NutPunch_SNPrintF(diff.now, sizeof(diff.now), "%s", now.data);
            NP_HandleEventCb(NPCB_LobbyMetadataChanged, &diff);

            break;
        }

        NP_SetVar(&NP_LobbyMetadata, now.name, now.data);
    }

done_getting_beat:
    if (old_master != new_master) {
        if (new_master == NutPunch_LocalPeer())
            NP_Info("We're the lobby's master now");
        NP_HandleEventCb(NPCB_NewMaster, &new_master);
    }

    NP_LastBeating = NutPunch_TimeNS();
}

static void NP_HandleListing(NP_Message msg) {
    if (!NP_AddrEq(msg.from, NP_ServerAddr))
        return;

    NutPunch_LobbyList list = {0};
    list.count = msg.len / sizeof(NutPunch_LobbyInfo);
    list.lobbies = list.count ? (NutPunch_LobbyInfo*)msg.data : NULL;

    NP_HandleEventCb(NPCB_LobbyList, &list);
}

static void NP_HandleLobbyMetadata(NP_Message msg) {
    if (!NP_AddrEq(msg.from, NP_ServerAddr))
        return;

    NutPunch_LobbyMetadata info = {0};

    NutPunch_Memcpy(info.lobby, msg.data, sizeof(info.lobby)), msg.data += sizeof(info.lobby);
    info.metadata = NULL;

    for (size_t i = 0; msg.data[0] && i < NUTPUNCH_MAX_FIELDS; i++) {
        NutPunch_Field* field = NutPunch_Malloc(sizeof(*field));
        field->next = info.metadata, info.metadata = field;
        NutPunch_Memcpy(field, msg.data, sizeof(NP_Field)), msg.data += sizeof(NP_Field);
    }

    NP_HandleEventCb(NPCB_LobbyMetadata, &info);

    NP_NukeMetadata(&info.metadata);
}

static void NP_HandleData(NP_Message msg) {
    if (NP_AddrEq(msg.from, NP_ServerAddr))
        return;

    NutPunch_Peer peer_idx = NP_FindPeer(msg.from);

    if (peer_idx == NUTPUNCH_MAX_PLAYERS)
        return;

    const NutPunch_Channel chan = (msg.len--, *msg.data++);
    if (chan >= NP_ChannelCount)
        return;

    NP_PeerInfo* const peer = &NP_Peers[peer_idx];

    NP_IncomingData* last = NULL;
    for (; last && last->next; last = last->next) {}

    last = *(last ? &last->next : &NP_Unread[chan])
        = (NP_IncomingData*)NutPunch_Malloc(sizeof(NP_IncomingData));

    last->peer = peer_idx, last->len = msg.len, last->next = NULL;
    last->data = NutPunch_Malloc(last->len);
    NutPunch_Memcpy(last->data, msg.data, last->len);
}

static void NP_HandleQueue(NP_Message msg) {
    if (NP_AddrEq(msg.from, NP_ServerAddr)) {
        NP_QueueTime = *msg.data++;
        NP_QueueCount = *msg.data++;
    }
}

static void NP_HandleDate(NP_Message msg) {
    if (NP_Mode == NPNM_Matchmaking && NP_AddrEq(msg.from, NP_ServerAddr)) {
        NP_Connect((char*)msg.data, true, NP_HB_Queue);
        NP_HandleEventCb(NPCB_QueueCompleted, msg.data);
    }
}

static void NP_HandleAcky(NP_Message msg) {
    for (NP_OutgoingPacket* cur = NP_Pending; cur; cur = cur->next)
        cur->acked |= (cur->id == ntohl(*(uint32_t*)msg.data));
}

static void NP_SendHeartbeat() {
    if (NP_Socket == NUTPUNCH_INVALID_SOCKET)
        return;

    static uint8_t heartbeat[sizeof(NP_Header) + sizeof(NP_Heartbeat) + sizeof(NP_Metadata)] = {0};
    NP_Memzero(heartbeat);

    uint8_t* ptr = heartbeat;

    NP_SockAddr addr = {0};
    socklen_t addr_size = sizeof(addr);
    getsockname(NP_Socket, (struct sockaddr*)&addr, &addr_size);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    switch (NP_Mode) {
    case NPNM_Normal:
        NutPunch_Memcpy(ptr, "JOIN", sizeof(NP_Header));
        ptr += sizeof(NP_Header);

        NutPunch_Memcpy(ptr, NP_PeerId, sizeof(NutPunch_PeerId));
        ptr += sizeof(NutPunch_PeerId);

        NutPunch_Memcpy(ptr, NP_LobbyId, sizeof(NutPunch_LobbyId));
        ptr += sizeof(NutPunch_LobbyId);

        *(NP_HeartbeatFlagsStorage*)ptr = NP_HeartbeatFlags,
        ptr += sizeof(NP_HeartbeatFlagsStorage);

        *(uint32_t*)ptr = addr.sin_addr.s_addr, ptr += 4;
        *(uint16_t*)ptr = addr.sin_port, ptr += 2;

        ptr += NP_DumpMetadata(ptr, NP_LobbyMetadata);

        break;

    case NPNM_Matchmaking:
        NutPunch_Memcpy(ptr, "FIND", sizeof(NP_Header));
        ptr += sizeof(NP_Header);

        NutPunch_Memcpy(ptr, NP_PeerId, sizeof(NutPunch_PeerId));
        ptr += sizeof(NutPunch_PeerId);

        NutPunch_Memcpy(ptr, NP_QueueId, sizeof(NutPunch_QueueId));
        ptr += sizeof(NutPunch_QueueId);

        break;

    default:
        return;
    }

    NP_JustSend(NP_ServerAddr, heartbeat, ptr - heartbeat, false);
}

static void NP_FlushPendingQueue() {
    if (NP_Socket == NUTPUNCH_INVALID_SOCKET)
        return;

    const NutPunch_Clock now = NutPunch_TimeNS();

    for (NP_OutgoingPacket *prev = NULL, *cur = NP_Pending; cur; cur = cur->next) {
        bool send = false, nuke = false;

        if (cur->retries < 0) {
            send = nuke = true;
        } else if (cur->last_retry) {
            const bool due = now - cur->last_retry > NUTPUNCH_RETRY_INTERVAL * NUTPUNCH_MS;
            nuke = cur->acked || due && cur->retries++ > NUTPUNCH_MAX_RETRIES;
            send = due && !nuke;
        } else {
            send = true, nuke = false;
        }

        if (send) {
            cur->last_retry = now;
            sendto(NP_Socket, cur->data, cur->len, 0, (struct sockaddr*)&cur->destination,
                sizeof(cur->destination));
        }

        if (nuke) {
            *(prev ? &prev->next : &NP_Pending) = cur->next;
            NutPunch_Free(cur->data), NutPunch_Free(cur);
        } else {
            prev = cur;
        }
    }
}

static int NP_UglyRecvFrom(NP_SockAddr* addr, void* buf, int buf_size) {
    socklen_t addr_size = sizeof(*addr);
    struct sockaddr* const shit_addr = (struct sockaddr*)addr;
    return recvfrom(NP_Socket, (char*)buf, buf_size, 0, shit_addr, &addr_size);
}

static void NP_ReceiveShit() {
    const int prefix = 4;

    for (;;) {
        NP_SockAddr addr = {0};
        static uint8_t buf[NUTPUNCH_FRAGMENT_SIZE] = {0};
        int size = NP_UglyRecvFrom(&addr, buf, sizeof(buf));

        if (size < 0) {
            if (NP_SockError() != NP_WouldBlock && NP_SockError() != NP_TooFat
                && NP_SockError() != NP_ConnReset)
            {
                NutPunch_Reset();
                NP_Warn("Things went haywire while receiving");
                NP_LastStatus = NPS_Error;
            }

            break;
        }

        size -= prefix + (int)sizeof(NP_Header);

        if (size < 0)
            continue; // junk

        uint32_t id = ntohl(*(uint32_t*)buf);
        if (id) { // ackies
            static uint8_t acky[sizeof(NP_Header) + 4] = "ACKY";
            *(uint32_t*)(acky + sizeof(NP_Header)) = htonl(id);
            NP_JustSend(addr, acky, sizeof(acky), false);
        }

        for (size_t i = 0; i < sizeof(NP_MessageTypes) / sizeof(*NP_MessageTypes); i++) {
            const NP_MessageType type = NP_MessageTypes[i];

            if (size < type.min_packet_size)
                continue;
            if (NutPunch_Memcmp(buf + prefix, type.identifier, sizeof(NP_Header)))
                continue;

            NP_Message msg = {0};
            msg.from = addr, msg.len = size;
            msg.data = (uint8_t*)(buf + prefix + sizeof(NP_Header));
            type.handle(msg);

            break;
        }
    }
}

static void NP_SendGoodbyes() {
    if (NP_Socket == NUTPUNCH_INVALID_SOCKET)
        return;

    if (NP_Mode == NPNM_Query)
        return;

    static uint8_t bye[sizeof(NP_Header) + sizeof(NutPunch_PeerId)] = "DISC";
    NutPunch_Memcpy(bye + sizeof(NP_Header), NP_PeerId, sizeof(NutPunch_PeerId));

    for (int i = 0; i < 10; i++)
        NP_JustSend(NP_ServerAddr, bye, sizeof(bye), false);
}

void NutPunch_Flush() {
    if (NP_Closing)
        NP_SendGoodbyes();
    NP_FlushPendingQueue();
}

void NutPunch_Register(NutPunch_CallbackEvent event, NutPunch_Callback cb) {
    if (event < NPCB_Count)
        NP_Callbacks[event] = cb;
}

static void NP_NetworkUpdate() {
    NP_SendHeartbeat();
    NP_ReceiveShit();
    NP_FlushPendingQueue();

    const NutPunch_Clock now = NutPunch_TimeNS();

    if (now - NP_LastBeating >= NUTPUNCH_TIMEOUT_INTERVAL * NUTPUNCH_MS) {
        NP_Warn("NutPuncher timed out");
        NP_LastStatus = NPS_Error;
        return;
    }

    for (NutPunch_Peer i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
        if (i != NutPunch_LocalPeer() && NutPunch_PeerAlive(i)
            && now - NP_Peers[i].last_ping >= NUTPUNCH_TIMEOUT_INTERVAL * NUTPUNCH_MS)
        {
            NP_KillPeer(i);
        }
    }
}

NutPunch_UpdateStatus NutPunch_Update() {
    NP_LazyInit();

    if (NP_LastStatus == NPS_Idle || NP_Socket == NUTPUNCH_INVALID_SOCKET)
        return NPS_Idle;

    NP_LastStatus = NPS_Online;
    NP_NetworkUpdate();

    if (NP_LastStatus == NPS_Error) {
        NutPunch_Disconnect();
        return NPS_Error;
    }

    return NP_LastStatus;
}

void NutPunch_Disconnect() {
    NP_Info("Disconnecting from lobby (if any)");
    if (NutPunch_IsOnline()) // send a disconnection packet too
        NP_Closing = true, NP_NetworkUpdate();
    NutPunch_Reset();
}

void NutPunch_SetChannelCount(int count) {
    if (count < 1 || count > NUTPUNCH_MAX_CHANNELS)
        return;
    NP_ChannelCount = count;
}

bool NutPunch_HasMessage(NutPunch_Channel chan) {
    return chan < NUTPUNCH_MAX_CHANNELS && NP_Unread[chan] != NULL;
}

int NutPunch_NextMessage(NutPunch_Channel chan, void* out, int* size) {
    if (chan >= NUTPUNCH_MAX_CHANNELS) {
        NP_Warn("We don't have %d channels bro", chan + 1);
        return NUTPUNCH_MAX_PLAYERS;
    }

    NP_IncomingData* const packet = NP_Unread[chan];

    if (!packet) {
        NP_Warn("You forgot to check `NutPunch_HasMessage(%d)`", chan);
        return NUTPUNCH_MAX_PLAYERS;
    }

    if (size && *size < packet->len) {
        NP_Warn("Not enough memory allocated to copy the next packet");
        return NUTPUNCH_MAX_PLAYERS;
    }

    if (size)
        *size = (int)packet->len;
    if (out)
        NutPunch_Memcpy(out, packet->data, packet->len);

    NP_Unread[chan] = packet->next;
    NutPunch_Free(packet->data);
    NutPunch_Free(packet);

    if (packet->peer >= NUTPUNCH_MAX_PLAYERS)
        return NUTPUNCH_MAX_PLAYERS;
    return packet->peer;
}

static void NP_SendPro(
    NutPunch_Channel channel, NutPunch_Peer peer, const void* data, int size, bool reliable) {
    if (!NutPunch_PeerAlive(peer) || size <= 0 || channel >= NUTPUNCH_MAX_CHANNELS)
        return;

    const size_t total_size = sizeof(NP_Header) + 1 + size;
    uint8_t *buf = NutPunch_Malloc(total_size), *ptr = buf + sizeof(NP_Header);

    NutPunch_Memcpy(buf, "DATA", sizeof(NP_Header));
    *ptr++ = channel, NutPunch_Memcpy(ptr, data, size);
    NP_JustSend(NP_Peers[peer].address, buf, total_size, reliable);
}

void NutPunch_Send(NutPunch_Channel channel, NutPunch_Peer peer, const void* data, int size) {
    NP_SendPro(channel, peer, data, size, false);
}

void
NutPunch_SendReliably(NutPunch_Channel channel, NutPunch_Peer peer, const void* data, int size) {
    NP_SendPro(channel, peer, data, size, true);
}

int NutPunch_PeerCount() {
    int count = 0;
    for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
        count += NutPunch_PeerAlive(i);
    return count;
}

bool NutPunch_PeerAlive(NutPunch_Peer peer) {
    if (peer >= NUTPUNCH_MAX_PLAYERS)
        return false;
    if (NutPunch_LocalPeer() == peer)
        return true;
    return !NP_AddrNull(NP_Peers[peer].address);
}

int NutPunch_LocalPeer() {
    if (!NutPunch_IsOnline())
        return NUTPUNCH_MAX_PLAYERS;
    return NP_LocalPeer;
}

int NutPunch_MasterPeer() {
    if (!NutPunch_IsOnline())
        return NUTPUNCH_MAX_PLAYERS;
    return NP_Master;
}

bool NutPunch_IsOnline() {
    return NP_LastStatus == NPS_Online;
}

bool NutPunch_IsReady() {
    return NutPunch_LocalPeer() != NUTPUNCH_MAX_PLAYERS;
}

const char* NutPunch_Basename(const char* path) {
    size_t len = 0;

    while (path[len])
        len++;

    for (size_t i = len - 2; i >= 0; i--)
        if (path[i] == '/' || path[i] == '\\')
            return &path[i + 1];

    return path;
}

NutPunch_Clock NutPunch_TimeNS() {
    struct timespec ts = {0};
    clock_gettime(CLOCK_REALTIME, &ts);
    return (NutPunch_Clock)ts.tv_sec * NUTPUNCH_SEC + (NutPunch_Clock)ts.tv_nsec;
}

#ifndef NUTPUNCH_WINDOSE

#include <errno.h>

void NP_SleepMs(int ms) {
    // Stolen from: <https://stackoverflow.com/a/1157217>
    struct timespec ts = {0};
    ts.tv_sec = ms / 1000, ts.tv_nsec = (ms % 1000) * (NUTPUNCH_SEC / 1000);
    int res = 0;
    do { res = nanosleep(&ts, &ts); } while (res && errno == EINTR);
}

#endif // NUTPUNCH_WINDOSE
