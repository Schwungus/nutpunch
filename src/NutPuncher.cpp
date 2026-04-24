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

#define NUTPUNCH_IMPLEMENTATION
#include <NutPunch.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

static constexpr const NutPunch_Clock KEEP_QUEUE_FOR = 20 * NUTPUNCH_SEC;

/// a little debouncing delay to prevent recreating a grindr queue right after timing it out.
static constexpr const NutPunch_Clock GRINDR_DEBOUNCE = 3 * NUTPUNCH_SEC;

static constexpr const size_t MAX_LOBBIES = 512;

static NutPunch_Clock elapsed(NutPunch_Clock start) {
    return NutPunch_TimeNS() - start;
}

static ENetHost* ENET = nullptr;

struct Lobby;
static std::unordered_map<std::string, Lobby> lobbies;

static const char* fmt_lobby_id(const std::string& id) {
    static char buf[sizeof(NutPunch_LobbyId) + 1] = {0};

    for (int i = 0; i < sizeof(NutPunch_LobbyId); i++) {
        const char c = id[i];
        const bool alpha = c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z',
                   numeric = c >= '0' && c <= '9';
        if (alpha || numeric || c == '-' || c == '_') {
            buf[i] = c;
        } else if (!c) {
            buf[i] = 0;
            return buf;
        } else {
            buf[i] = ' ';
        }
    }

    for (int i = sizeof(NutPunch_LobbyId); i > 0; i--) {
        if (buf[i - 1] != ' ' && buf[i - 1] != 0) {
            buf[i] = 0;
            return buf;
        }
    }

    return buf;
}

static bool match_field_value(const int diff, const int flags) {
    bool result = false;

    if (flags & NPF_Greater)
        result |= diff < 0;
    else if (flags & NPF_Less)
        result |= diff > 0;

    if (flags & NPF_Eq)
        result |= diff == 0;

    return (flags & NPF_Not) ? !result : result;
}

static void just_send(ENetPeer* peer, const void* buf, size_t len, uint32_t flags) {
    if (!peer)
        return;

    ENetPacket* const packet = enet_packet_create(buf, len, flags);

    if (enet_peer_send(peer, 0, packet))
        enet_packet_destroy(packet);
}

static void gtfo(ENetPeer* peer, NutPunch_ErrorCode error) {
    static uint8_t buf[sizeof(NP_Header) + 1] = "GTFO";
    buf[sizeof(NP_Header)] = error;

    for (int i = 0; i < 10; i++) // we dgaf about reliability, we're just proving a point......
        just_send(peer, buf, sizeof(buf), 0);
}

struct Field : NutPunch_Field {
    Field() {
        reset();
    }

    explicit operator bool() const {
        return size && name[0];
    }

    bool named(const char* name) const {
        if (!name || !*name || !*this)
            return false;

        int arg_len = static_cast<int>(std::strlen(name));
        if (arg_len > NUTPUNCH_FIELD_NAME_MAX)
            arg_len = NUTPUNCH_FIELD_NAME_MAX;

        int our_len = NUTPUNCH_FIELD_NAME_MAX;
        for (int i = 1; i < NUTPUNCH_FIELD_NAME_MAX; i++) {
            if (!this->name[i]) {
                our_len = i;
                break;
            }
        }

        return our_len == arg_len && !std::memcmp(this->name, name, our_len);
    }

    bool matches(const NutPunch_Filter& filter) const {
        const int diff = std::memcmp(data, filter.field.value, size);
        return match_field_value(diff, filter.comparison);
    }

    void reset() {
        std::memset(name, 0, sizeof(name));
        std::memset(data, 0, sizeof(data));
        size = 0;
    }
};

struct Metadata {
    Field fields[NUTPUNCH_MAX_FIELDS];

    Metadata() {
        reset();
    }

    Field* find(const char* name) {
        const int idx = index_of(name);
        return NUTPUNCH_MAX_FIELDS == idx ? nullptr : &fields[idx];
    }

    void insert(const Field& field) {
        if (!field.size)
            return;

        const int idx = index_of(field.name);
        if (NUTPUNCH_MAX_FIELDS == idx)
            return;

        int name_size = static_cast<int>(std::strlen(field.name));
        if (name_size > NUTPUNCH_FIELD_NAME_MAX)
            name_size = NUTPUNCH_FIELD_NAME_MAX;

        int data_size = field.size;
        if (data_size > NUTPUNCH_FIELD_DATA_MAX)
            data_size = NUTPUNCH_FIELD_DATA_MAX;

        auto& target = fields[idx];
        std::memcpy(target.name, field.name, name_size);
        std::memcpy(target.data, field.data, data_size);
        target.size = data_size;
    }

    void load(const char* ptr) {
        for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++)
            insert(reinterpret_cast<const Field*>(ptr)[i]);
    }

    void reset() {
        std::memset(fields, 0, sizeof(fields));
    }

  private:
    int index_of(const char* name) {
        if (!name || !*name)
            return NUTPUNCH_MAX_FIELDS;

        // first matching
        for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++)
            if (fields[i].named(name))
                return i;

        // or first uninitialized
        for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++)
            if (!fields[i])
                return i;

        return NUTPUNCH_MAX_FIELDS; // or bust
    }
};

struct Player {
    NutPunch_Peer index = 0;
    std::string id;
    ENetPeer* enet;

    Player(NutPunch_Peer index, const std::string& id, ENetPeer* enet)
        : index(index), id(id), enet(enet) {}

    ~Player() {
        // HACK: not calling `reset()` here since that would close the `enet` peer which is managed
        // EXTERNALLY by the `ENET` host.
        //
        // also, `enet` is essentially a shared pointer, so closing it without reference-counting
        // breaks a lot of things...
    }

    explicit operator bool() const {
        return !id.empty() && enet;
    }

    void boot() {
        id.clear();

        if (enet)
            enet_peer_disconnect_now(enet, 0);
        enet = nullptr;
    }
};

struct Lobby {
    const std::string id;
    bool unlisted = false;
    uint8_t capacity = NUTPUNCH_MAX_PLAYERS;
    std::vector<Player> players;
    Metadata metadata;

    Lobby() {}

    Lobby(const std::string& id) : id(id) {}

    const char* fmt_id() const {
        return fmt_lobby_id(id);
    }

    void update() {
        for (auto& player : players)
            beat(player);
        std::erase_if(players, [](const auto& player) { return !player; });
    }

    int special(uint8_t idx) const {
        switch (idx) {
        case NPSF_Capacity:
            return capacity;
        case NPSF_Players:
            return (int)players.size();
        default:
            return 0;
        }
    }

    explicit operator bool() const {
        return players.size() > 0;
    }

    NutPunch_Peer index_of(const std::string& id) const {
        for (size_t i = 0; i < players.size(); i++)
            if (players[i] && id == players[i].id)
                return i;
        return NUTPUNCH_MAX_PLAYERS;
    }

    bool has(const std::string& id) const {
        return index_of(id) != NUTPUNCH_MAX_PLAYERS;
    }

    void accept(const std::string& id, ENetPeer* enet, const NP_HeartbeatFlagsStorage flags,
        const char* meta) {
        NutPunch_Peer idx = index_of(id);
        bool just_joined = false;

        if (idx == NUTPUNCH_MAX_PLAYERS) {
            for (idx = 0; idx < (NutPunch_Peer)players.size(); idx++) {
                bool good = true;
                for (const auto& player : players) {
                    if (player.index == idx) {
                        good = false;
                        break;
                    }
                }
                if (good)
                    break;
            }

            just_joined = true;

            if (idx >= NUTPUNCH_MAX_PLAYERS) {
                gtfo(enet, NPE_LobbyFull);
                return;
            }

            players.emplace_back(idx, id, enet);
        }

        if (just_joined)
            NP_Info("Player %d joined lobby '%s'", idx + 1, fmt_id());

        if (idx == master()) {
            unlisted = flags & NP_HB_Unlisted;
            capacity = 1 + (flags >> 4);
            metadata.load(meta);
        }
    }

    void beat(Player& player) {
        if (player.id.empty())
            return;

        static uint8_t buf[sizeof(NP_Header) + sizeof(NP_Beating)] = "BEAT";
        uint8_t* ptr = buf + sizeof(NP_Header);

        *ptr++ = unlisted;
        *ptr++ = player.index;
        *ptr++ = master();
        *ptr++ = capacity;

        static constexpr const size_t addrs_size = NUTPUNCH_MAX_PLAYERS * sizeof(NP_PeerAddr);
        std::memset(ptr, 0, addrs_size);

        for (const auto& player : players) {
            if (!player)
                continue;

            const auto addr = player.enet->address;
            const size_t off = player.index * sizeof(NP_PeerAddr);

            const uint16_t port = htons(addr.port);
            memcpy(ptr + off + 0, &addr.host, 4);
            memcpy(ptr + off + 4, &port, 2);
        }
        ptr += addrs_size;

        std::memcpy(ptr, &metadata, sizeof(Metadata));
        ptr += sizeof(Metadata);

        just_send(player.enet, buf, sizeof(buf), 0);
    }

    void kill_bro(const NutPunch_PeerId id, ENetPeer* enet) {
        for (auto& player : players) {
            if (player.id != id && player.enet != enet)
                continue;
            NP_Info("Player %s disconnected", player.id.c_str());
            player.boot();
        }
    }

    bool match_against(const NutPunch_Filter* filters, size_t filter_count) const {
        for (int f = 0; f < filter_count; f++) {
            const auto& filter = filters[f];

            if (filter.field.alwayszero != 0) {
                int diff = (uint8_t)filter.special.value;
                diff -= special(filter.special.index);
                if (match_field_value(diff, filter.comparison))
                    goto next_filter;
                return false;
            }

            for (int m = 0; m < NUTPUNCH_MAX_FIELDS; m++) {
                const auto& field = metadata.fields[m];
                if (!field.named(filter.field.name))
                    continue;
                if (field.matches(filter))
                    goto next_filter;
            }

            return false; // no field matched the filter
        next_filter:
            continue;
        }

        // All filters matched.
        return true;
    }

    NutPunch_Peer master() {
        if (master_idx < players.size() && players[master_idx])
            return master_idx;
        for (master_idx = 0; master_idx < (NutPunch_Peer)players.size(); master_idx++)
            if (players[master_idx])
                break;
        return master_idx;
    }

  private:
    NutPunch_Peer master_idx = NUTPUNCH_MAX_PLAYERS;
};

struct Grindr {
    const std::string queue_id;
    NutPunch_LobbyId lobby_id = {0};
    NutPunch_Clock last_match = NutPunch_TimeNS();
    std::unordered_map<std::string, Player> players;
    bool closing = false;

    Grindr(const std::string& queue_id) : queue_id(queue_id) {}

    explicit operator bool() const {
        return !closing && elapsed(last_match) <= KEEP_QUEUE_FOR + GRINDR_DEBOUNCE;
    }

    void accept(const std::string& peer_id, ENetPeer* peer) {
        if (players.contains(peer_id))
            return;

        players.emplace(peer_id, Player(0, peer_id, peer));
        closing = false;

        NP_Info("QUEUE: Added peer '%s' (%s)", peer_id.c_str(), queue_id.c_str());
    }

    void update() {
        if (closing)
            return;

        if (players.size() <= 1 && elapsed(last_match) > KEEP_QUEUE_FOR) {
            if (!closing)
                for (const auto& [id, p] : players)
                    gtfo(p.enet, NPE_QueueNoMatch);

            closing = true;
            return;
        }

        while (players.size() >= 2) // highly unlikely but i like taking it rough :)
            LETSGOO();

        const auto num_players = players.size();
        if (!num_players)
            return;

        uint8_t buf[sizeof(NP_Header) + sizeof(uint8_t) + sizeof(uint16_t)] = "QUEU";
        uint8_t* ptr = buf + sizeof(NP_Header);

        *ptr = (KEEP_QUEUE_FOR - elapsed(last_match)) / NUTPUNCH_SEC;
        ptr += sizeof(uint8_t);

        *(uint16_t*)ptr = htons(num_players - 1);
        ptr += sizeof(uint16_t);

        for (const auto& [id, p] : players)
            just_send(p.enet, buf, sizeof(buf), 0);
    }

    void LETSGOO() {
        auto pair1 = std::move(*players.begin());
        players.erase(players.begin());

        auto pair2 = std::move(*players.begin());
        players.erase(players.begin());

        NutPunch_LobbyId lobby_id = {0};
        for (int i = 0; i < sizeof(lobby_id); i++)
            lobby_id[i] = (char)('A' + (std::rand() % 26));

        static uint8_t buf[sizeof(NP_Header) + sizeof(NutPunch_LobbyId)] = "DATE";
        std::memcpy(buf + sizeof(NP_Header), lobby_id, sizeof(lobby_id));

        for (const auto enet : {pair1.second.enet, pair2.second.enet})
            just_send(enet, buf, sizeof(buf), NP_Send_Reliably);

        NP_Info("QUEUE: Matched peers '%s' and '%s' to lobby '%s'", pair1.first.c_str(),
            pair2.first.c_str(), fmt_lobby_id(lobby_id));
    }
};

static std::unordered_map<std::string, Grindr> matchmaking;

static bool create_lobby(const std::string& id, ENetPeer* peer) {
    if (lobbies.size() >= MAX_LOBBIES) {
        gtfo(peer, NPE_NoSuchLobby);
        NP_Warn("Reached lobby limit");
        return false;
    }

    // Match against existing peers to prevent creating multiple lobbies with the same master.
    for (const auto& [lobby_id, lobby] : lobbies) {
        for (const auto& player : lobby.players)
            if (player && player.enet == peer)
                return true; // fuck you...
    }

    lobbies.insert({id, Lobby(id)});
    NP_Info("Created lobby '%s'", fmt_lobby_id(id.c_str()));
    return true;
}

static void send_lobbies(ENetPeer* peer, const char* target_id) {
    constexpr const size_t pnrsize = sizeof(NutPunch_LobbyId) + sizeof(NutPunch_Metadata);
    static uint8_t buf[sizeof(NP_Header) + pnrsize] = "LGMA";
    uint8_t* ptr = buf + sizeof(NP_Header);

    std::memcpy(ptr, target_id, sizeof(NutPunch_LobbyId));
    ptr += sizeof(NutPunch_LobbyId);

    if (!lobbies.contains(target_id))
        return;

    const auto& lobby = lobbies.at(target_id);

    if (lobby.unlisted)
        return;

    for (const auto& field : lobby.metadata.fields) {
        std::memcpy(ptr, &field, sizeof(NutPunch_Field));
        ptr += sizeof(NutPunch_Field);
    }

    just_send(peer, buf, sizeof(buf), 0);
}

static void send_lobbies(ENetPeer* peer, size_t filter_count, const NutPunch_Filter* filters) {
    constexpr const size_t fuckyou = NUTPUNCH_MAX_SEARCH_RESULTS * sizeof(NutPunch_LobbyInfo);
    static uint8_t buf[sizeof(NP_Header) + fuckyou] = "LIST"; // BITCH REFORMATS EVERY COMMIT
    uint8_t* ptr = buf + sizeof(NP_Header);

    size_t count = 0;

    for (const auto& [id, lobby] : lobbies) {
        if (lobby.unlisted || !lobby.match_against(filters, filter_count))
            continue;

        std::memcpy(ptr, id.data(), std::strlen(lobby.fmt_id()));
        ptr += sizeof(NutPunch_LobbyId);
        *ptr++ = lobby.players.size(), *ptr++ = lobby.capacity;

        if (++count >= NUTPUNCH_MAX_SEARCH_RESULTS)
            break;
    }

    just_send(peer, buf, ptr - buf, 0);
}

static void kill_bro(const NutPunch_PeerId peer_id, ENetPeer* enet) {
    for (auto& [id, lobby] : lobbies)
        lobby.kill_bro(peer_id, enet);

    for (auto& [id, queue] : matchmaking) {
        std::erase_if(queue.players, [peer_id, enet](const auto& pair) {
            const auto& [id, peer] = pair;
            if (peer.enet != enet && std::memcmp(id.data(), peer_id, sizeof(NutPunch_PeerId)))
                return false;
            NP_Info("QUEUE: Peer '%s' disconnected", id.c_str());
            return true;
        });
    }
}

static void handle_recv(ENetEvent event) {
    int rcv = (int)event.packet->dataLength - (int)sizeof(NP_Header);
    if (rcv < 0)
        return; // junk...

    const char *buf = (char*)event.packet->data, *ptr = buf + sizeof(NP_Header);

    if (!std::memcmp(buf, "LIST", sizeof(NP_Header))) {
        if (rcv == sizeof(NutPunch_LobbyId)) {
            send_lobbies(event.peer, ptr);
        } else if (!(rcv % sizeof(NutPunch_Filter))) {
            const auto filtars = reinterpret_cast<const NutPunch_Filter*>(ptr);
            send_lobbies(event.peer, rcv / sizeof(NutPunch_Filter), filtars);
        } else {
            // junk...
        }
    } else if (rcv == sizeof(NP_Find) && !std::memcmp(buf, "FIND", sizeof(NP_Header))) {
        std::string peer_id(ptr, sizeof(NutPunch_PeerId));
        ptr += sizeof(NutPunch_PeerId);

        std::string queue_id(ptr, sizeof(NutPunch_QueueId));
        ptr += sizeof(NutPunch_QueueId);

        if (!matchmaking.contains(queue_id))
            matchmaking.emplace(queue_id, Grindr(queue_id));
        matchmaking.at(queue_id).accept(peer_id, event.peer);
    } else if (rcv >= sizeof(NutPunch_PeerId) && !std::memcmp(buf, "DISC", sizeof(NP_Header))) {
        kill_bro(ptr, event.peer);
    } else if (rcv == sizeof(NP_Heartbeat) && !std::memcmp(buf, "JOIN", sizeof(NP_Header))) {
        std::string peer_id(ptr, sizeof(NutPunch_PeerId));
        ptr += sizeof(NutPunch_PeerId);

        std::string lobby_id(ptr, sizeof(NutPunch_LobbyId));
        ptr += sizeof(NutPunch_LobbyId);

        auto flags = *(const NP_HeartbeatFlagsStorage*)ptr;
        ptr += sizeof(flags);

        NutPunch_ErrorCode err = NPE_Ok;

        if (lobbies.count(lobby_id)) {
            if (!(flags & (NP_HB_JoinExisting | NP_HB_Queue)) && !lobbies[lobby_id].has(peer_id))
                err = NPE_LobbyExists;
        } else if (flags & NP_HB_JoinExisting) {
            err = NPE_NoSuchLobby;
        } else if (!create_lobby(lobby_id, event.peer)) {
            return;
        }

        if (err == NPE_Ok)
            lobbies[lobby_id].accept(peer_id, event.peer, flags, ptr);
        else
            gtfo(event.peer, err);
    } else {
        // most likely junk...
    }
}

static void receive() {
    ENetEvent event;

    while (enet_host_service(ENET, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_RECEIVE:
            handle_recv(event);
            enet_packet_destroy(event.packet);
            break;

        case ENET_EVENT_TYPE_DISCONNECT:
            kill_bro("", event.peer);
            break;

        case ENET_EVENT_TYPE_CONNECT:
        default:
            break;
        }
    }
}

static void update_lobbies() {
    for (auto& [id, lobby] : lobbies)
        lobby.update();

    std::erase_if(lobbies, [](const auto& kv) {
        const auto& lobby = kv.second;
        if (lobby)
            return false;
        NP_Info("Deleting lobby '%s'", lobby.fmt_id());
        return true;
    });
}

static void update_grindr() {
    for (auto& [id, queue] : matchmaking)
        queue.update();

    std::erase_if(matchmaking, [](const auto& pair) {
        const auto& [id, queue] = pair;
        if (queue)
            return false;
        NP_Info("QUEUE: Deleting queue '%s'", id.c_str());
        return true;
    });
}

struct ENetGuard {
    ENetGuard() {
        enet_initialize();

        ENetAddress addr = {0};
        addr.host = ENET_HOST_ANY;
        addr.port = NUTPUNCH_SERVER_PORT;

        static constexpr const size_t max_conns = 512; // TODO: revise
        ENET = enet_host_create(&addr, max_conns, 1, 0, 0);
    }

    ~ENetGuard() {
        if (ENET)
            enet_host_destroy(ENET);
        ENET = nullptr;

        enet_deinitialize();
    }
};

int main(int argc, char*[]) {
    if (argc == 4) { // deploy-script hack to print the server port
        std::printf("%d\n", NUTPUNCH_SERVER_PORT);
        return EXIT_SUCCESS;
    }

    std::srand(NutPunch_TimeNS());
    ENetGuard _enet;

    constexpr const NutPunch_Clock MIN_DELTA = NUTPUNCH_SEC / 30;
    NP_Info("Running on port %d", NUTPUNCH_SERVER_PORT);

    for (;;) {
        const NutPunch_Clock start = NutPunch_TimeNS();

        if (!ENET) {
            NP_Warn("SOCKET DIED!!!");
            return EXIT_FAILURE;
        }

        receive();
        update_grindr();
        update_lobbies();
        enet_host_flush(ENET);

        const NutPunch_Clock delta = elapsed(start);
        if (delta < MIN_DELTA)
            NP_SleepMs((MIN_DELTA - delta) / NUTPUNCH_MS);
    }

    return EXIT_SUCCESS;
}
