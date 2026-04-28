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

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#define NUTPUNCH_IMPLEMENTATION
#include <NutPunch.h>

static constexpr const NutPunch_Clock PEER_TIMEOUT = 3000 * NUTPUNCH_MS;

static constexpr const NutPunch_Clock KEEP_QUEUE_FOR = 20 * NUTPUNCH_SEC;

/// a little debouncing delay to prevent recreating a grindr queue right after timing it out.
static constexpr const NutPunch_Clock GRINDR_DEBOUNCE = 3 * NUTPUNCH_SEC;

static constexpr const size_t MAX_LOBBIES = 512;

static NP_Sock SOCK = NUTPUNCH_INVALID_SOCKET;

static NutPunch_Clock elapsed(NutPunch_Clock start = 0) {
    return NutPunch_TimeNS() - start;
}

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

static void just_send(NP_SockAddr addr, const void* buf, size_t len) {
    const int prefix = 4;

    if (prefix + len > NUTPUNCH_FRAGMENT_SIZE)
        return; // womp womp womp

    if (NP_AddrNull(addr) || SOCK == NUTPUNCH_INVALID_SOCKET)
        return;

    const auto prepend = std::make_unique<char[]>(prefix + len);
    *reinterpret_cast<uint32_t*>(prepend.get()) = htonl(0);
    memcpy(prepend.get() + prefix, buf, len);

    const auto shit = (const struct sockaddr*)&addr;
    sendto(SOCK, prepend.get(), prefix + (int)len, 0, shit, sizeof(addr));
}

static void gtfo(NP_SockAddr addr, NutPunch_ErrorCode error) {
    static uint8_t buf[sizeof(NP_Header) + 1] = "GTFO";
    buf[sizeof(NP_Header)] = error;

    for (int i = 0; i < 10; i++) // we dgaf about reliability, we're just proving a point......
        just_send(addr, buf, sizeof(buf));
}

struct Metadata {
    std::unordered_map<std::string, std::string> fields;

    Metadata() {}

    void insert(const std::string& name, const std::string& data) {
        if (name.empty())
            return;
        if (fields.size() < NUTPUNCH_MAX_FIELDS)
            fields.insert_or_assign(name, data);
    }

    int dump(void* rawout) const {
        auto outf = reinterpret_cast<NP_Field*>(rawout);

        for (const auto& pair : fields) {
            NutPunch_SNPrintF(outf->name, sizeof(outf->name), "%s", pair.first.c_str());
            NutPunch_SNPrintF(outf->data, sizeof(outf->data), "%s", pair.second.c_str());
            outf++;
        }

        return (int)((char*)outf - (char*)rawout);
    }

    void load(const char* ptr, size_t num_fields) {
        for (int i = 0; i < num_fields; i++) {
            const auto field = reinterpret_cast<const NP_Field*>(ptr)[i];
            const std::string name(field.name, strnlen(field.name, NUTPUNCH_FIELD_NAME_MAX)),
                data(field.data, strnlen(field.data, NUTPUNCH_FIELD_DATA_MAX));
            insert(name, data);
        }
    }

    void reset() {
        fields.clear();
    }
};

struct Player {
    NutPunch_Peer index = 0;
    NP_SockAddr pub, same_nat;
    std::string id;
    NutPunch_Clock last_beat;

    Player(NutPunch_Peer index, NP_SockAddr pub, NP_SockAddr same_nat, const std::string& id)
        : index(index), pub(pub), same_nat(same_nat), id(id), last_beat(elapsed()) {}

    void beat() {
        last_beat = elapsed();
    }
};

static void cleanup_players_list(std::vector<Player>& players) {
    std::erase_if(players, [](const auto& player) {
        if (elapsed(player.last_beat) >= PEER_TIMEOUT) {
            NP_Info("%s timed out", player.id.c_str());
            return true;
        }

        return false;
    });
}

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
        cleanup_players_list(players);
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
            if (id == players[i].id)
                return i;
        return NUTPUNCH_MAX_PLAYERS;
    }

    bool has(const std::string& id) const {
        return index_of(id) != NUTPUNCH_MAX_PLAYERS;
    }

    void accept(const std::string& id, NP_SockAddr pub, const NP_HeartbeatFlagsStorage flags,
        const char* buf, const char* ptr, size_t rcv) {
        NP_SockAddr same_nat = {0};

        same_nat.sin_family = AF_INET;
        same_nat.sin_addr.s_addr = *(uint32_t*)ptr, ptr += 4;
        same_nat.sin_port = *(uint16_t*)ptr, ptr += 2;

        if (!ntohl(same_nat.sin_addr.s_addr))
            same_nat.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

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
                gtfo(pub, NPE_LobbyFull);
                return;
            }

            players.emplace_back(idx, pub, same_nat, id);
        }

        if (just_joined)
            NP_Info("Player %d joined lobby '%s'", idx + 1, fmt_id());

        for (auto& player : players) {
            if (player.id == id) {
                player.beat();
                break;
            }
        }

        if (idx == master()) {
            unlisted = flags & NP_HB_Unlisted;
            capacity = 1 + (flags >> 4);
            metadata.load(ptr, (rcv - (ptr - buf)) / sizeof(NP_Field));
        }
    }

    void beat(Player& player) {
        static uint8_t buf[sizeof(NP_Header) + sizeof(NP_BeatingAppend)] = "BEAT";
        uint8_t* ptr = buf + sizeof(NP_Header);

        *ptr++ = unlisted;
        *ptr++ = player.index;
        *ptr++ = master();
        *ptr++ = (NutPunch_Peer)players.size();
        *ptr++ = capacity;

        for (const auto& player : players) {
            *ptr++ = player.index;

            memcpy(ptr, &player.pub.sin_addr.s_addr, 4), ptr += 4;
            memcpy(ptr, &player.pub.sin_port, 2), ptr += 2;

            memcpy(ptr, &player.same_nat.sin_addr.s_addr, 4), ptr += 4;
            memcpy(ptr, &player.same_nat.sin_port, 2), ptr += 2;
        }

        ptr += metadata.dump(ptr); // FIXME: OVERFLOWS HERE!!!
        NP_Info("DAMN %llu", ptr - buf);

        just_send(player.pub, buf, ptr - buf);
    }

    void kill_bro(const NutPunch_PeerId id, NP_SockAddr pub) {
        std::erase_if(players, [id, pub](const auto& player) {
            if (player.id != id && !NP_AddrEq(player.pub, pub))
                return false;
            NP_Info("Player %s disconnected gracefully", player.id.c_str());
            return true;
        });
    }

    bool match_against(const NutPunch_Filter* filters, size_t filter_count) const {
        for (int f = 0; f < filter_count; f++) {
            const auto& filter = filters[f];

            if (filter.field.alwayszero != 0) {
                int diff = (uint8_t)filter.special.value;
                diff -= special(filter.special.index);

                if (match_field_value(diff, filter.comparison))
                    continue;

                return false;
            }

            const auto& field = filter.field;
            const std::string name(field.name, strnlen(field.name, NUTPUNCH_FIELD_NAME_MAX));

            if (!metadata.fields.contains(name))
                return false;

            const std::string& data = metadata.fields.at(name);
            const int diff = std::memcmp(
                data.c_str(), filter.field.value, strnlen(field.value, NUTPUNCH_FIELD_DATA_MAX));

            if (match_field_value(diff, filter.comparison))
                continue;

            return false;
        }

        return true;
    }

    NutPunch_Peer master() {
        for (const auto& player : players)
            return player.index;
        return NUTPUNCH_MAX_PLAYERS;
    }
};

// TODO: fucking nuke.
struct Grindr {
    const std::string queue_id;
    NutPunch_Clock last_match = NutPunch_TimeNS();
    std::unordered_map<std::string, Player> players;
    bool closing = false;

    Grindr(const std::string& queue_id) : queue_id(queue_id) {}

    explicit operator bool() const {
        return elapsed(last_match) <= KEEP_QUEUE_FOR + GRINDR_DEBOUNCE;
    }

    void accept(const std::string& peer_id, NP_SockAddr pub) {
        if (players.contains(peer_id))
            return;

        players.emplace(peer_id, Player(0, pub, {0}, peer_id));
        closing = false;

        NP_Info("QUEUE: Added peer '%s' (%s)", peer_id.c_str(), queue_id.c_str());
    }

    void update() {
        if (closing)
            return;

        constexpr const size_t match = 2;

        if (players.size() < match && elapsed(last_match) > KEEP_QUEUE_FOR) {
            if (!closing) {
                for (const auto& [id, player] : players)
                    gtfo(player.pub, NPE_QueueNoMatch);

                players.clear();
                closing = true;
            }

            return;
        }

        while (players.size() >= match) // highly unlikely to loop but i like taking it rough :)
            LETSGOO();

        const size_t num_players = players.size();
        if (!num_players)
            return;

        static uint8_t buf[sizeof(NP_Header) + 1 + 1] = "QUEU";
        uint8_t* ptr = buf + sizeof(NP_Header);

        const NutPunch_Clock since = elapsed(last_match),
                             diff = KEEP_QUEUE_FOR > since ? KEEP_QUEUE_FOR - since : 0;

        *ptr++ = (uint8_t)std::min(NutPunch_Clock(255), diff / NUTPUNCH_SEC);
        *ptr++ = (uint8_t)std::min(size_t(255), num_players - 1);

        for (const auto& [id, player] : players)
            just_send(player.pub, buf, sizeof(buf));
    }

    void LETSGOO() {
        auto pair1 = std::move(*players.begin());
        players.erase(players.begin());

        auto pair2 = std::move(*players.begin());
        players.erase(players.begin());

        std::string lobby_id;
        for (int i = 0; i < sizeof(NutPunch_LobbyId); i++)
            lobby_id.push_back((char)('A' + (std::rand() % 26)));

        static uint8_t buf[sizeof(NP_Header) + sizeof(NutPunch_LobbyId)] = "DATE";
        std::memcpy(buf + sizeof(NP_Header), lobby_id.data(), sizeof(NutPunch_LobbyId));

        for (const auto& pub : {pair1.second.pub, pair2.second.pub})
            just_send(pub, buf, sizeof(buf));

        NP_Info("QUEUE: Matched peers '%s' and '%s' to lobby '%s'", pair1.first.c_str(),
            pair2.first.c_str(), fmt_lobby_id(lobby_id));
    }
};

static std::unordered_map<std::string, Grindr> matchmaking;

static bool create_lobby(const std::string& id, NP_SockAddr pub) {
    if (lobbies.size() >= MAX_LOBBIES) {
        gtfo(pub, NPE_NoSuchLobby);
        NP_Warn("Reached lobby limit");
        return false;
    }

    // Match against existing peers to prevent creating multiple lobbies with the same master.
    for (const auto& [lobby_id, lobby] : lobbies) {
        for (const auto& player : lobby.players)
            if (NP_AddrEq(player.pub, pub))
                return true; // fuck you...
    }

    lobbies.insert({id, Lobby(id)});
    NP_Info("Created lobby '%s'", fmt_lobby_id(id));
    return true;
}

static void send_lobby_metadata(NP_SockAddr pub, const char* target_id) {
    constexpr const size_t pnrsize = sizeof(NutPunch_LobbyId) + sizeof(NP_Metadata);
    static uint8_t buf[sizeof(NP_Header) + pnrsize] = "LGMA";

    if (!lobbies.contains(target_id))
        return;

    const auto& lobby = lobbies.at(target_id);

    if (lobby.unlisted)
        return;

    uint8_t* ptr = buf + sizeof(NP_Header);

    std::memcpy(ptr, target_id, sizeof(NutPunch_LobbyId));
    ptr += sizeof(NutPunch_LobbyId);

    lobby.metadata.dump(ptr);

    just_send(pub, buf, sizeof(buf));
}

static void send_lobbies(NP_SockAddr pub, size_t filter_count, const NutPunch_Filter* filters) {
    constexpr const size_t fuckyou = NUTPUNCH_MAX_SEARCH_RESULTS * sizeof(NutPunch_LobbyInfo);
    static uint8_t buf[sizeof(NP_Header) + fuckyou] = "LIST"; // BITCH REFORMATS EVERY COMMIT

    if (filter_count > NUTPUNCH_MAX_SEARCH_FILTERS)
        return;

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

    just_send(pub, buf, ptr - buf);
}

static void kill_bro(const NutPunch_PeerId peer_id, NP_SockAddr pub) {
    for (auto& [id, lobby] : lobbies)
        lobby.kill_bro(peer_id, pub);

    for (auto& [id, queue] : matchmaking) {
        std::erase_if(queue.players, [peer_id, pub](const auto& pair) {
            const auto& [id, player] = pair;

            if (!NP_AddrEq(player.pub, pub) && id != peer_id)
                return false;

            NP_Info("QUEUE: Peer '%s' disconnected", id.c_str());
            return true;
        });
    }
}

static void handle_recv(NP_SockAddr pub, const char* buf, int rcv) {
    rcv -= 4 + (int)sizeof(NP_Header);
    if (rcv < 0)
        return; // junk...

    // we completely ignore the packet id on the NutPuncher side for now
    buf += 4;
    const char* ptr = buf + sizeof(NP_Header);

    if (!std::memcmp(buf, "LIST", sizeof(NP_Header))) {
        if (rcv == sizeof(NutPunch_LobbyId)) {
            send_lobby_metadata(pub, ptr);
        } else if (rcv % sizeof(NutPunch_Filter) == 0) {
            const auto filtars = reinterpret_cast<const NutPunch_Filter*>(ptr);
            send_lobbies(pub, rcv / sizeof(NutPunch_Filter), filtars);
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
        matchmaking.at(queue_id).accept(peer_id, pub);
    } else if (rcv >= sizeof(NutPunch_PeerId) && !std::memcmp(buf, "DISC", sizeof(NP_Header))) {
        kill_bro(ptr, pub);
    } else if (rcv >= sizeof(NP_Heartbeat) && !std::memcmp(buf, "JOIN", sizeof(NP_Header))) {
        std::string peer_id(ptr, sizeof(NutPunch_PeerId));
        ptr += sizeof(NutPunch_PeerId);

        size_t len = 0;
        for (; len < sizeof(NutPunch_LobbyId); len++)
            if (!ptr[len])
                break;

        std::string lobby_id(ptr, len);
        ptr += sizeof(NutPunch_LobbyId);

        auto flags = *(const NP_HeartbeatFlagsStorage*)ptr;
        ptr += sizeof(flags);

        NutPunch_ErrorCode err = NPE_Ok;

        if (lobbies.count(lobby_id)) {
            if (!(flags & (NP_HB_JoinExisting | NP_HB_Queue)) && !lobbies[lobby_id].has(peer_id))
                err = NPE_LobbyExists;
        } else if (flags & NP_HB_JoinExisting) {
            err = NPE_NoSuchLobby;
        } else if (!create_lobby(lobby_id, pub)) {
            return;
        }

        if (err == NPE_Ok)
            lobbies[lobby_id].accept(peer_id, pub, flags, buf, ptr, rcv);
        else
            gtfo(pub, err);
    } else {
        // most likely junk...
    }
}

static void receive() {
    static char buf[NUTPUNCH_FRAGMENT_SIZE] = {0};

    for (;;) {
        NP_SockAddr addr = {0};
        socklen_t addr_size = sizeof(addr);
        int size = recvfrom(SOCK, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&addr, &addr_size);

        if (size < 0) {
            if (NP_SockError() != NP_WouldBlock && NP_SockError() != NP_TooFat
                && NP_SockError() != NP_ConnReset)
            {
                NP_Warn("recvfrom fail: %d", NP_SockError());
            }

            break;
        }

        handle_recv(addr, buf, size);
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

struct Guard {
    Guard() {
#ifdef NUTPUNCH_WINDOSE
        WSADATA bitch = {0};
        WSAStartup(MAKEWORD(2, 2), &bitch);
#endif
    }

    ~Guard() {
        NP_NukeSocket(&SOCK);

#ifdef NUTPUNCH_WINDOSE
        WSACleanup();
#endif
    }
};

int main(int argc, char*[]) {
    if (argc == 4) { // deploy-script hack to print the server port
        std::printf("%d\n", NUTPUNCH_SERVER_PORT);
        return EXIT_SUCCESS;
    }

    std::srand(NutPunch_TimeNS());
    Guard _linganguliguliguli;

    SOCK = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (SOCK == NUTPUNCH_INVALID_SOCKET || !NP_MakeNonblocking(SOCK) || !NP_MakeReuseAddr(SOCK))
        return EXIT_FAILURE;

    {
        NP_SockAddr local = {0};
        local.sin_family = AF_INET;
        local.sin_port = htons(NUTPUNCH_SERVER_PORT);
        local.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(SOCK, (struct sockaddr*)&local, sizeof(local)))
            return EXIT_FAILURE;
    }

    constexpr const NutPunch_Clock MIN_DELTA = NUTPUNCH_SEC / 30;
    NP_Info("Running on port %d", NUTPUNCH_SERVER_PORT);

    for (;;) {
        const NutPunch_Clock start = NutPunch_TimeNS();

        if (SOCK == NUTPUNCH_INVALID_SOCKET) {
            NP_Warn("SOCKET DIED!!!");
            return EXIT_FAILURE;
        }

        receive();
        update_grindr();
        update_lobbies();

        const NutPunch_Clock delta = elapsed(start);
        if (delta < MIN_DELTA)
            NP_SleepMs((MIN_DELTA - delta) / NUTPUNCH_MS);
    }

    return EXIT_SUCCESS;
}
