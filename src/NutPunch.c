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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "NutPunch.h"

typedef struct {
    NutPunch_Metadata metadata;
    ENetPeer* enet; // only used by `NP_SendPings`
} NP_PeerInfo;

typedef struct {
    ENetPeer* from;
    int len;
    const uint8_t* data;
    NutPunch_Channel chan;
} NP_Message;

typedef struct NP_PacketQueue {
    NutPunch_Peer peer;
    void* data;
    size_t len;
    struct NP_PacketQueue* next;
} NP_PacketQueue;

typedef struct {
    const char identifier[sizeof(NP_Header) + 1];
    void (*const handle)(NP_Message);
    const int64_t packet_size;
} NP_MessageType;

static void NP_HandlePing(NP_Message), NP_HandleGTFO(NP_Message), NP_HandleBeating(NP_Message),
    NP_HandleListing(NP_Message), NP_HandleLobbyMetadata(NP_Message), NP_HandleData(NP_Message),
    NP_HandlePong(NP_Message), NP_HandleDate(NP_Message);

#define NP_ANY_LEN (-1)
#define NP_PING_SIZE (sizeof(NP_Header) + 1 + sizeof(NutPunch_Metadata))

static const NP_MessageType NP_MessageTypes[] = {
    {"PING", NP_HandlePing,          1 + sizeof(NutPunch_Metadata)                       },
    {"LIST", NP_HandleListing,       NP_ANY_LEN                                          },
    {"LGMA", NP_HandleLobbyMetadata, sizeof(NutPunch_LobbyId) + sizeof(NutPunch_Metadata)},
    {"DATA", NP_HandleData,          NP_ANY_LEN                                          },
    {"GTFO", NP_HandleGTFO,          1                                                   },
    {"BEAT", NP_HandleBeating,       sizeof(NP_Beating)                                  },
    {"PONG", NP_HandlePong,          0                                                   },
    {"DATE", NP_HandleDate,          sizeof(NutPunch_LobbyId)                            },
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

static NutPunch_Clock NP_LastBeating = 0;

static bool NP_InitDone = false, NP_Closing = false;
static NutPunch_UpdateStatus NP_LastStatus = NPS_Idle;

static char NP_LobbyId[sizeof(NutPunch_LobbyId) + 1] = {0};
static NutPunch_PeerId NP_PeerId = {0};
static NutPunch_QueueId NP_QueueId = {0};

static NP_PeerInfo NP_Peers[NUTPUNCH_MAX_PLAYERS] = {0};
static NutPunch_Peer NP_LocalPeer = NUTPUNCH_MAX_PLAYERS, NP_Master = NUTPUNCH_MAX_PLAYERS,
                     NP_MaxPlayers = 0;

static NutPunch_Callback NP_Callbacks[NPCB_Count] = {0};

static ENetHost* NP_ENetHost = NULL;
static ENetPeer* NP_PuncherPeer = NULL;

static ENetAddress NP_ServerAddr = {0};
static char NP_ServerHost[128] = {0};

static NutPunch_Channel NP_ChannelCount = 1;
static NP_PacketQueue* NP_Unread[NUTPUNCH_MAX_CHANNELS] = {0};

static bool NP_Unlisted = false;
static NutPunch_Metadata NP_LobbyMetadata = {0}, NP_PeerMetadata = {0};

static NP_NetMode NP_Mode = NPNM_Normal;
static NP_HeartbeatFlagsStorage NP_HeartbeatFlags = 0;

static void
NP_JustSend(ENetPeer* peer, uint8_t channel, const void* buf, size_t len, uint32_t flags) {
    if (!peer)
        return;

    ENetPacket* packet = enet_packet_create(buf, len, flags);
    if (enet_peer_send(peer, channel, packet))
        enet_packet_destroy(packet);
}

static void NP_NukeLobbyDataLite() {
    NP_Mode = NPNM_Normal;
    NP_Closing = NP_Unlisted = false;
    NP_LocalPeer = NP_Master = NUTPUNCH_MAX_PLAYERS;
    NP_Memzero(NP_Peers);

    if (NP_PuncherPeer)
        enet_peer_disconnect_now(NP_PuncherPeer, 0);
    NP_PuncherPeer = NULL;

    if (NP_ENetHost)
        enet_host_destroy(NP_ENetHost);
    NP_ENetHost = NULL;
}

static void NP_NukeLobbyData() {
    NP_NukeLobbyDataLite();
    NP_Memzero(NP_LobbyMetadata), NP_Memzero(NP_PeerMetadata);
}

static void NP_ResetImpl() {
    NP_LastBeating = NutPunch_TimeNS();
    NP_NukeLobbyData();

    NP_LobbyId[0] = 0, NP_HeartbeatFlags = 0;
    NP_MemzeroRef(NP_ServerAddr), NP_Memzero(NP_Peers);
    NP_LastStatus = NPS_Idle;

    for (size_t i = 0; i < NUTPUNCH_MAX_CHANNELS; i++) {
        while (NP_Unread[i]) {
            NP_PacketQueue* ptr = NP_Unread[i];
            NP_Unread[i] = ptr->next;
            NutPunch_Free(ptr->data);
            NutPunch_Free(ptr);
        }
    }
}

static void NP_LazyInit() {
    if (NP_InitDone)
        return;
    NP_InitDone = true;

    enet_initialize();

    srand(NutPunch_TimeNS());
    for (int i = 0; i < sizeof(NutPunch_PeerId); i++)
        NP_PeerId[i] = (char)('A' + rand() % 26);

    NP_ResetImpl();

    void (*const println)(const char*, ...) = (NP_Logger ? NP_Logger : NP_DefaultLogger);
    println(".-------------------------------------------------------------.");
    println("| For troubleshooting multiplayer connectivity, please visit: |");
    println("|    https://github.com/Schwungus/nutpunch#troubleshooting    |");
    println("'-------------------------------------------------------------'");
}

void NutPunch_Shutdown() {
    NutPunch_Disconnect();
    enet_deinitialize();
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

static const void* NP_GetVar(const NutPunch_Field* fields, const char* name, int* size) {
    static uint8_t buf[NUTPUNCH_FIELD_DATA_MAX] = {0};
    NP_Memzero(buf);

    int name_size = NP_FieldNameSize(name);
    if (!name_size || !fields)
        goto none;

    for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
        const NutPunch_Field* const field = &fields[i];
        if (name_size != NP_FieldNameSize(field->name))
            continue;
        if (NutPunch_Memcmp(field->name, name, name_size))
            continue;
        if (field->size > sizeof(buf)) {
            NP_Warn("Metadata field size exceeds buffer size");
            goto none;
        }
        NutPunch_Memcpy(buf, field->data, field->size);
        if (size)
            *size = field->size;
        return buf;
    }

none:
    if (size)
        *size = 0;
    return NULL;
}

static void NP_SetVar(NutPunch_Field* fields, const char* name, int size, const void* data) {
    if (!fields)
        return;

    const int name_size = NP_FieldNameSize(name);
    if (!name_size)
        return;

    if (!data) {
        NP_Warn("No data?");
        return;
    }

    if (size < 1) {
        NP_Warn("Invalid metadata field size!");
        return;
    }

    if (size > NUTPUNCH_FIELD_DATA_MAX) {
        NP_Warn("Trimming metadata field from %d to %d bytes", size, NUTPUNCH_FIELD_DATA_MAX);
        size = NUTPUNCH_FIELD_DATA_MAX;
    }

    for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
        static const NutPunch_Field nullfield = {0};
        NutPunch_Field* const field = &fields[i];

        if (NutPunch_Memcmp(field, &nullfield, sizeof(nullfield))) {
            if (NP_FieldNameSize(field->name) != name_size)
                continue;
            if (NutPunch_Memcmp(field->name, name, name_size))
                continue;
        }

        NP_Memzero(field->name);
        NutPunch_Memcpy(field->name, name, name_size);

        NP_Memzero(field->data);
        NutPunch_Memcpy(field->data, data, size);

        field->size = size;
        return;
    }
}

void NutPunch_RequestLobbyData(const NutPunch_LobbyId lobby) {
    if (!NP_PuncherPeer)
        return;

    static uint8_t buf[sizeof(NP_Header) + sizeof(NutPunch_LobbyId)] = "LIST";
    NutPunch_Memcpy(buf + sizeof(NP_Header), lobby, sizeof(NutPunch_LobbyId));

    NP_JustSend(NP_PuncherPeer, 0, buf, sizeof(buf), 0);
}

const void* NutPunch_GetLobbyData(const char* name, int* size) {
    return NP_GetVar(NP_LobbyMetadata, name, size);
}

const void* NutPunch_GetPeerData(NutPunch_Peer peer, const char* name, int* size) {
    return NP_GetVar(NP_GetPeerFields(peer), name, size);
}

void NutPunch_SetLobbyData(const char* name, int size, const void* data) {
    NP_SetVar(NP_LobbyMetadata, name, size, data);
}

void NutPunch_SetPeerData(const char* name, int size, const void* data) {
    NP_SetVar(NP_PeerMetadata, name, size, data);
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

    // TODO: revise the magic numbers.
    ENetAddress addr = {ENET_HOST_ANY, ENET_PORT_ANY};
    NP_ENetHost = enet_host_create(&addr, 128, 1 + NP_ChannelCount, 0, 0);

    if (!NP_ENetHost) {
        NutPunch_Reset(), NP_LastStatus = NPS_Error;
        NP_Warn("Failed to bind a socket!");
        return false;
    }

    if (!NP_ServerHost[0]) {
        NutPunch_SetServerAddr(NUTPUNCH_DEFAULT_SERVER);
        NP_Info("Connecting to the public NutPuncher as none was explicitly specified");
    }

    NP_MemzeroRef(NP_ServerAddr);
    enet_address_set_host(&NP_ServerAddr, NP_ServerHost);
    NP_ServerAddr.port = NUTPUNCH_SERVER_PORT;
    NP_PuncherPeer = enet_host_connect(NP_ENetHost, &NP_ServerAddr, 1, 0);

    if (!NP_PuncherPeer) {
        NutPunch_Reset(), NP_LastStatus = NPS_Error;
        NP_Warn("Failed to connect to NutPuncher!");
        return false;
    }

    NP_HeartbeatFlags = flags;
    NP_LastBeating = NutPunch_TimeNS();

    NP_Info("Ready to send heartbeats");
    NP_LastStatus = NPS_Online;
    NP_Memzero(NP_LastError);

    if (lobby_id)
        NutPunch_SNPrintF(NP_LobbyId, sizeof(NP_LobbyId), "%s", lobby_id);
    return true;
}

bool NutPunch_QueryMode() {
    if (!NP_Connect(NULL, false, 0))
        return false;
    NP_Mode = NPNM_Query;
    return true;
}

bool NutPunch_Host(const char* lobby_id) {
    return NP_Connect(lobby_id, true, 0);
}

bool NutPunch_Join(const char* lobby_id) {
    return NP_Connect(lobby_id, true, NP_HB_JoinExisting);
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
    const int DEFAULT = 4;

    if (players <= 1 || players > NUTPUNCH_MAX_PLAYERS) {
        NP_Warn("Setting %d players max (requested %d)", DEFAULT, players);
        players = DEFAULT;
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
    if (!NP_PuncherPeer)
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

    NP_JustSend(NP_PuncherPeer, 0, query, ptr - query, 0);
}

const char* NutPunch_GetLastError() {
    return NP_LastError;
}

static void NP_HandleEventCb(NutPunch_CallbackEvent event, const void* data) {
    if (NP_Callbacks[event])
        NP_Callbacks[event](data);
}

static const char* NP_FormatSockaddr(ENetAddress addr) {
    static char buf[64] = "";

    const char* s = inet_ntoa(*(struct in_addr*)&addr.host);
    NutPunch_SNPrintF(buf, sizeof(buf), "[%s]:%d", s, addr.port);

    return buf;
}

static void NP_KillPeer(NutPunch_Peer peer) {
    if (peer >= NUTPUNCH_MAX_PLAYERS)
        return;

    if (NutPunch_PeerAlive(peer))
        NP_HandleEventCb(NPCB_PeerLeft, &peer);

    NP_PeerInfo* ptr = &NP_Peers[peer];
    if (ptr->enet)
        enet_peer_reset(ptr->enet);

    NP_MemzeroRef(*ptr);
}

static bool NP_AddrNull(ENetAddress addr) {
    return !addr.host && !addr.port;
}

static void NP_HandlePing(NP_Message msg) {
    const NutPunch_Peer idx = *msg.data++;
    if (idx >= NUTPUNCH_MAX_PLAYERS)
        return;

    const bool was_dead = !NutPunch_PeerAlive(idx);
    msg.from->data = &NP_Peers[idx];

    NP_PeerInfo* const peer = &NP_Peers[idx];
    peer->enet = msg.from;

    static NutPunch_PeerFieldDiff changed[NUTPUNCH_MAX_FIELDS] = {0};
    NP_Memzero(changed);

    for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
        NutPunch_Field *then = &peer->metadata[i], *now = (NutPunch_Field*)msg.data;
        msg.data += sizeof(NutPunch_Field);

        changed[i].peer = idx, changed[i].then = *then, changed[i].now = *now;
        *then = *now;
    }

    if (was_dead)
        NP_HandleEventCb(NPCB_PeerJoined, &idx);

    // makes more sense to emit peer metadata changes AFTER they've been joined.
    for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++)
        if (NutPunch_Memcmp(&changed[i].then, &changed[i].now, NUTPUNCH_FIELD_DATA_MAX))
            NP_HandleEventCb(NPCB_PeerMetadataChanged, &changed[i]);
}

static void NP_HandleGTFO(NP_Message msg) {
    if (msg.from != NP_PuncherPeer)
        return;

    // Have to work around designated array initializers for C++ NutPuncher to compile...
    const char* errors[NPE_Max] = {0};
    errors[NPE_NoSuchLobby] = "Lobby doesn't exist";
    errors[NPE_LobbyExists] = "Lobby already exists";
    errors[NPE_LobbyFull] = "Lobby is full";
    errors[NPE_QueueNoMatch] = "No players to match with";
    errors[NPE_Sybau] = "sybau :wilted_rose:";

    int idx = msg.data[0];
    if (idx <= NPE_Ok || idx >= NPE_Max)
        NP_Warn("Unidentified error");
    else
        NP_Warn("%s", errors[idx]);
    NP_LastStatus = NPS_Error;
}

static void NP_PrintOurAddresses(const uint8_t* data) {
    ENetAddress addr = {0};

    NutPunch_Memcpy(&addr.host, data, 4), data += 4;
    addr.port = ntohs(*(uint16_t*)data), data += 2;

    NP_Info("Server thinks you are %s", NP_FormatSockaddr(addr));
    NP_Info("Same-NAT address: %s", NP_FormatSockaddr(NP_ENetHost->address));
}

static NutPunch_Peer NP_FindEnetPeer(ENetPeer* peer) {
    for (NutPunch_Peer i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
        if (NP_Peers[i].enet == peer)
            return i;
    return NUTPUNCH_MAX_PLAYERS;
}

static void NP_SendPings(int idx, const uint8_t* data) {
    if (!NP_ENetHost)
        return;

    if (idx == NP_LocalPeer)
        return;

    ENetAddress pub = {0};

    NutPunch_Memcpy(&pub.host, data, 4), data += 4;
    pub.port = ntohs(*(uint16_t*)data), data += 2;

    if (NP_AddrNull(pub)) { // they're dead on the NutPuncher's side
        NP_KillPeer(idx);
        return;
    }

    NP_PeerInfo* const peer = &NP_Peers[idx];

    if (!peer->enet)
        peer->enet = enet_host_connect(NP_ENetHost, &pub, 1 + NP_ChannelCount, 0);

    static uint8_t ping[NP_PING_SIZE] = "PING";
    uint8_t* ptr = &ping[sizeof(NP_Header)];

    *ptr++ = NP_LocalPeer;
    NutPunch_Memcpy(ptr, NP_PeerMetadata, sizeof(NutPunch_Metadata));

    NP_JustSend(peer->enet, 0, ping, sizeof(ping), 0);
}

static void NP_HandleBeating(NP_Message msg) {
    if (msg.from != NP_PuncherPeer)
        return;

    NP_LastBeating = NutPunch_TimeNS();

    const bool just_joined = NP_LocalPeer == NUTPUNCH_MAX_PLAYERS;
    const NutPunch_Peer old_master = NutPunch_MasterPeer();

    NP_Unlisted = *msg.data++;
    NP_LocalPeer = *msg.data++;
    NP_Master = *msg.data++;
    NP_MaxPlayers = *msg.data++;

    if (NP_LocalPeer >= NUTPUNCH_MAX_PLAYERS) {
        NP_Warn("NutPuncher sent us a junk response?!");
        return;
    }

    // add the join-existing flag after a successful join because otherwise we could get a
    // random-ass disconnection with the "Lobby already exists!" error message.
    NP_HeartbeatFlags |= NP_HB_JoinExisting;

    // sync outgoing max player count with the one we just received.
    NP_HeartbeatFlags &= 0xF;
    NP_HeartbeatFlags |= (NutPunch_GetMaxPlayers() - 1) << 4;

    for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
        NP_SendPings(i, msg.data);
        if (i == NP_LocalPeer && just_joined)
            NP_PrintOurAddresses(msg.data);
        msg.data += sizeof(NP_PeerAddr);
    }

    for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
        NutPunch_Field* then = &NP_LobbyMetadata[i];
        NutPunch_Field* now = (NutPunch_Field*)msg.data;
        msg.data += sizeof(NutPunch_Field);

        if (!NutPunch_Memcmp(then, now, sizeof(NutPunch_Field)))
            continue;

        NutPunch_FieldDiff diff = {0};
        diff.then = *then, diff.now = *now;
        *then = *now;

        NP_HandleEventCb(NPCB_LobbyMetadataChanged, &diff);
    }

    const NutPunch_Peer new_master = NutPunch_MasterPeer();
    if (old_master != new_master) {
        if (new_master == NutPunch_LocalPeer())
            NP_Info("We're the lobby's master now");
        NP_HandleEventCb(NPCB_NewMaster, &new_master);
    }
}

static void NP_HandleListing(NP_Message msg) {
    if (msg.from != NP_PuncherPeer)
        return;

    NP_LastBeating = NutPunch_TimeNS();

    NutPunch_LobbyList list = {0};
    list.count = msg.len / sizeof(NutPunch_LobbyInfo);
    list.lobbies = list.count ? (NutPunch_LobbyInfo*)msg.data : NULL;

    NP_HandleEventCb(NPCB_LobbyList, &list);
}

static void NP_HandleLobbyMetadata(NP_Message msg) {
    if (msg.from != NP_PuncherPeer || msg.len < sizeof(NutPunch_LobbyId))
        return;

    NP_LastBeating = NutPunch_TimeNS();

    NutPunch_LobbyMetadata info = {0};

    NutPunch_Memcpy(info.lobby, msg.data, sizeof(NutPunch_LobbyId));
    msg.data += sizeof(NutPunch_LobbyId);
    info.count = (msg.len - sizeof(NutPunch_LobbyId)) / sizeof(NutPunch_Field);
    info.metadata = info.count ? (NutPunch_Field*)msg.data : NULL;

    NP_HandleEventCb(NPCB_LobbyMetadata, &info);
}

static void NP_HandleData(NP_Message msg) {
    if (msg.from == NP_PuncherPeer)
        return;

    NutPunch_Peer peer_idx = NP_FindEnetPeer(msg.from);

    if (peer_idx == NUTPUNCH_MAX_PLAYERS)
        return;

    if (!msg.chan) // channel 0 is reserved for NutPunch communications
        return;

    const NutPunch_Channel chan = msg.chan - 1;

    if (chan >= NP_ChannelCount)
        return;

    NP_PeerInfo* const peer = &NP_Peers[peer_idx];
    NP_PacketQueue* next = NULL;

    if (!NP_Unread[chan]) {
        NP_Unread[chan] = (NP_PacketQueue*)NutPunch_Malloc(sizeof(NP_PacketQueue));
        next = NP_Unread[chan];
    }

    for (NP_PacketQueue* cur = NP_Unread[chan]; cur && !next; cur = cur->next) {
        if (!cur->next) {
            cur->next = (NP_PacketQueue*)NutPunch_Malloc(sizeof(NP_PacketQueue));
            next = cur->next;
            break;
        }
    }

    if (next) {
        next->peer = peer_idx, next->len = msg.len, next->next = NULL;
        next->data = NutPunch_Malloc(next->len);
        NutPunch_Memcpy(next->data, msg.data, next->len);
    }
}

static void NP_HandlePong(NP_Message msg) {
    if (msg.from == NP_PuncherPeer)
        NP_LastBeating = NutPunch_TimeNS();
}

static void NP_HandleDate(NP_Message msg) {
    NP_Warn("SHIT");

    if (NP_Mode != NPNM_Matchmaking)
        return;

    if (msg.from != NP_PuncherPeer)
        return;

    NP_Connect((char*)msg.data, true, NP_HB_Queue);
    NP_HandleEventCb(NPCB_QueueCompleted, msg.data);
}

static void NP_SendHeartbeat() {
    if (!NP_PuncherPeer)
        return;

    static uint8_t heartbeat[sizeof(NP_Header) + sizeof(NP_Heartbeat)] = {0};
    NP_Memzero(heartbeat);

    uint8_t* ptr = heartbeat;

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

        NutPunch_Memcpy(ptr, NP_LobbyMetadata, sizeof(NutPunch_Metadata));
        ptr += sizeof(NutPunch_Metadata);

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

    NP_JustSend(NP_PuncherPeer, 0, heartbeat, ptr - heartbeat, 0);
}

static void NP_TickENetHost() {
    ENetEvent event;

    while (enet_host_service(NP_ENetHost, &event, 0) > 0) {
        // happens after handling a GTFO:
        if (NP_LastStatus == NPS_Error || !NP_PuncherPeer)
            break;

        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            break;
        }

        case ENET_EVENT_TYPE_DISCONNECT:
            NP_KillPeer(NP_FindEnetPeer(event.peer));
            event.peer->data = NULL;
            break;

        case ENET_EVENT_TYPE_RECEIVE: {
            int size = (int)event.packet->dataLength - (int)sizeof(NP_Header);
            if (size < 0)
                return; // junk

            uint8_t* const buf = event.packet->data;
            const size_t len = sizeof(NP_MessageTypes) / sizeof(*NP_MessageTypes);

            for (int i = 0; i < len; i++) {
                const NP_MessageType type = NP_MessageTypes[i];

                if (type.packet_size != NP_ANY_LEN && size != type.packet_size)
                    continue;
                if (NutPunch_Memcmp(buf, type.identifier, sizeof(NP_Header)))
                    continue;

                NP_Message msg = {0};
                msg.from = event.peer, msg.len = size, msg.chan = event.channelID;
                msg.data = (uint8_t*)(buf + sizeof(NP_Header));
                type.handle(msg);
                break;
            }

            enet_packet_destroy(event.packet);
            break;
        }

        default:
            break;
        }
    }
}

static void NP_SendGoodbyes() {
    if (!NP_PuncherPeer)
        return;

    if (NP_Mode == NPNM_Query)
        return;

    static uint8_t bye[sizeof(NP_Header) + sizeof(NutPunch_PeerId)] = "DISC";
    NutPunch_Memcpy(bye + sizeof(NP_Header), NP_PeerId, sizeof(NutPunch_PeerId));

    for (int i = 0; i < 10; i++)
        NP_JustSend(NP_PuncherPeer, 0, bye, sizeof(bye), 0);
}

void NutPunch_Flush() {
    if (NP_Closing)
        NP_SendGoodbyes();
    enet_host_flush(NP_ENetHost);
}

void NutPunch_Register(NutPunch_CallbackEvent event, NutPunch_Callback cb) {
    if (event < NPCB_Count)
        NP_Callbacks[event] = cb;
}

static void NP_NetworkUpdate() {
    NutPunch_Clock now = NutPunch_TimeNS(),
                   server_timeout = NUTPUNCH_SERVER_TIMEOUT_INTERVAL * NUTPUNCH_MS;

    if (NP_Mode != NPNM_Query && (now - NP_LastBeating) >= server_timeout) {
        NP_Warn("NutPuncher connection timed out!");
        NP_LastStatus = NPS_Error;
    } else {
        NP_SendHeartbeat();
        NP_TickENetHost();
    }
}

NutPunch_UpdateStatus NutPunch_Update() {
    NP_LazyInit();

    if (NP_LastStatus == NPS_Idle || !NP_ENetHost)
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

bool NutPunch_HasMessage(NutPunch_Channel channel) {
    return NP_Unread[channel] != NULL;
}

int NutPunch_NextMessage(NutPunch_Channel channel, void* out, int* size) {
    NP_PacketQueue* const packet = NP_Unread[channel];
    if (!packet) {
        NP_Warn("You forgot to check `NutPunch_HasMessage(%d)`", channel);
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

    NP_Unread[channel] = packet->next;
    NutPunch_Free(packet->data);
    NutPunch_Free(packet);

    if (packet->peer >= NUTPUNCH_MAX_PLAYERS)
        return NUTPUNCH_MAX_PLAYERS;
    return packet->peer;
}

void NutPunch_Send(
    NutPunch_Channel channel, NutPunch_Peer peer, uint32_t flags, const void* data, int size) {
    if (!NutPunch_PeerAlive(peer)) {
        NP_Warn("Sending to a dead peer should result in your own death too");
        return;
    }

    if (size <= 0)
        return;

    if (channel >= NUTPUNCH_MAX_CHANNELS)
        return;

    const size_t total_size = sizeof(NP_Header) + size;
    uint8_t *buf = NutPunch_Malloc(total_size), *ptr = buf + sizeof(NP_Header);

    NutPunch_Memcpy(buf, "DATA", sizeof(NP_Header));
    NutPunch_Memcpy(ptr, data, size);

    NP_JustSend(
        NP_Peers[peer].enet, 1 + channel, buf, total_size, flags | ENET_PACKET_FLAG_NO_ALLOCATE);
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

    ENetPeer* enet = NP_Peers[peer].enet;
    if (!enet)
        return false;
    return enet->data == &NP_Peers[peer]; // INSANE DOUBLE BACKREF HACK
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
    for (len = 0; path[len]; len++) {}
    for (size_t i = len - 2; i >= 0; i--)
        if (path[i] == '/' || path[i] == '\\')
            return &path[i + 1];
    return path;
}

// `TIME_UTC` & `timespec_get` polyfill for picky compilers.
#ifndef TIME_UTC

#define TIME_UTC 1

static int timespec_get(struct timespec* ts, int base) {
    if (base == TIME_UTC)
        return clock_gettime(CLOCK_REALTIME, ts) ? 0 : base;
    return 0;
}

#endif // TIME_UTC

NutPunch_Clock NutPunch_TimeNS() {
    struct timespec ts = {0};
    timespec_get(&ts, TIME_UTC);
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
