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

static constexpr const size_t MAX_LOBBIES = 1024;

static NP_Sock SOCK = NUTPUNCH_INVALID_SOCKET;

static NutPunch_Clock elapsed(NutPunch_Clock start = 0) {
    return NutPunch_TimeNS() - start;
}

struct Message {
    NP_SockAddr from;
    const char* data;
    int len;

    std::string read(size_t count) {
        const char* x = data;
        data += count, len -= (int)count;
        return std::string(x, count);
    }

    std::string read0term(size_t n) {
        const char* x = data;
        data += n, len -= (int)n;
        return std::string(x, strnlen(x, n));
    }

    template <typename T> T read() {
        const char* x = data;
        data += sizeof(T), len -= (int)sizeof(T);
        return *(const T*)x;
    }
};

struct Lobby;

struct LobbyId {
    std::string game, name;

    LobbyId() {}

    LobbyId(const std::string& game, const std::string& name) : game(game), name(name) {}

    bool operator==(const LobbyId& other) const {
        return game == other.game && name == other.name;
    }
};

namespace std {

template <> struct hash<LobbyId> {
    std::size_t operator()(const LobbyId& id) const noexcept {
        return std::hash<std::string>{}(id.game) ^ (std::hash<std::string>{}(id.name) << 1);
    }
};

} // namespace std

static std::unordered_map<LobbyId, Lobby> lobbies;

static const char* fmt_lobby_name(const std::string& id) {
    static char buf[sizeof(NutPunch_LobbyName) + 1] = {0};

    for (int i = 0; i < sizeof(NutPunch_LobbyName); i++) {
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

    for (int i = sizeof(NutPunch_LobbyName); i > 0; i--) {
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

    bool insert(const std::string& name, const std::string& data) {
        if (name.empty())
            return false;
        if (fields.size() >= NUTPUNCH_MAX_FIELDS)
            return false;
        fields.insert_or_assign(name, data);
        return true;
    }

    int dump(void* rawout) const {
        auto out = reinterpret_cast<char*>(rawout);

        for (const auto& pair : fields) {
            const char *name = pair.first.c_str(), *data = pair.second.c_str();

            auto len = pair.first.length() + 1;
            NutPunch_SNPrintF(out, len, "%s", name);
            out += len;

            len = pair.second.length() + 1;
            NutPunch_SNPrintF(out, len, "%s", data);
            out += len;
        }

        return (int)(out - (char*)rawout);
    }

    void load(const char* ptr, size_t len) {
        const char *start = ptr, *in = ptr;

        NutPunch_FieldName name;
        NutPunch_FieldValue data;

        while (in < start + len) {
            in = NP_ReadUntilNull(name, sizeof(name), start, in, len);
            in = NP_ReadUntilNull(data, sizeof(data), start, in, len);

            if (insert(name, data))
                NP_Trace("\"%s\" = \"%s\"", name, data);
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
    std::string name, game;

    uint8_t capacity = 1; // HACK: capacity is set to 1 when the lobby is created, but then it's set
                          // to the actual value when the host joins and heartbeats

    bool unlisted = true; // same hack here...

    std::vector<Player> players;
    Metadata metadata;

    Lobby() {} // only needed for stuffing this in a vector

    Lobby(const std::string& game, const std::string& name) : game(game), name(name) {}

    const char* fmt_id() const {
        return fmt_lobby_name(name);
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

    void accept(const std::string& id, const NP_HeartbeatFlagsStorage flags, Message msg) {
        NP_SockAddr same_nat = {0};

        same_nat.sin_family = AF_INET;
        same_nat.sin_addr.s_addr = msg.read<uint32_t>();
        same_nat.sin_port = msg.read<uint16_t>();

        // https://docs.libuv.org/en/v1.x/udp.html#c.uv_udp_send
        if (!ntohl(same_nat.sin_addr.s_addr))
            same_nat.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (index_of(id) == NUTPUNCH_MAX_PLAYERS) {
            if (players.size() >= capacity) {
                gtfo(msg.from, NPE_LobbyFull);
                return;
            }

            NutPunch_Peer idx = 0;

            for (; idx < capacity; idx++) {
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

            players.emplace_back(idx, msg.from, same_nat, id);
            NP_Info("Player %d joined lobby '%s'", idx + 1, fmt_id());
        }

        for (auto& player : players) {
            if (player.id == id) {
                player.beat();
                break;
            }
        }

        if (index_of(id) == master()) {
            unlisted = flags & NP_HB_Unlisted;
            capacity = 1 + (flags >> 4);
            metadata.load(msg.data, msg.len);
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

        ptr += metadata.dump(ptr);

        just_send(player.pub, buf, ptr - buf);
    }

    void kill_bro(const NutPunch_PeerId id, NP_SockAddr pub) {
        std::erase_if(players, [id, pub](const auto& player) {
            if (player.id != id && !NP_AddrEq(player.pub, pub))
                return false;
            NP_Info("Player %d disconnected gracefully", player.index + 1);
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
            const std::string name(field.name, strnlen(field.name, sizeof(NutPunch_FieldName)));

            if (!metadata.fields.contains(name))
                return false;

            const std::string& data = metadata.fields.at(name);
            const int diff = std::memcmp(data.c_str(), filter.field.value,
                strnlen(field.value, sizeof(NutPunch_FieldValue)));

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
    const std::string game_id;
    NutPunch_Clock last_match = NutPunch_TimeNS();
    std::unordered_map<std::string, Player> players;
    bool closing = false;

    static constexpr const size_t MATCH = 2;

    Grindr(const std::string& game_id) : game_id(game_id) {}

    explicit operator bool() const {
        return elapsed(last_match) <= KEEP_QUEUE_FOR + GRINDR_DEBOUNCE;
    }

    void accept(const std::string& peer_id, NP_SockAddr pub) {
        if (players.contains(peer_id))
            return;

        players.emplace(peer_id, Player(0, pub, {0}, peer_id));
        closing = false;

        NP_Info("QUEUE: Added peer '%s' (%s)", peer_id.c_str(), game_id.c_str());
    }

    void update() {
        if (closing)
            return;

        if (players.size() < MATCH && elapsed(last_match) > KEEP_QUEUE_FOR) {
            if (!closing) {
                for (const auto& [id, player] : players)
                    gtfo(player.pub, NPE_QueueNoMatch);

                players.clear();
                closing = true;
            }

            return;
        }

        while (players.size() >= MATCH) // highly unlikely to loop but i like taking it rough :)
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
        for (int i = 0; i < sizeof(NutPunch_LobbyName); i++)
            lobby_id.push_back((char)('A' + (std::rand() % 26)));

        static uint8_t buf[sizeof(NP_Header) + sizeof(NutPunch_LobbyName)] = "DATE";
        std::memcpy(buf + sizeof(NP_Header), lobby_id.data(), sizeof(NutPunch_LobbyName));

        for (const auto& pub : {pair1.second.pub, pair2.second.pub})
            just_send(pub, buf, sizeof(buf));

        NP_Info("QUEUE: Matched peers '%s' and '%s' to lobby '%s'", pair1.first.c_str(),
            pair2.first.c_str(), fmt_lobby_name(lobby_id));
    }
};

static std::unordered_map<std::string, Grindr> matchmaking;

static NutPunch_ErrorCode
create_lobby(const std::string& game, const std::string& name, NP_SockAddr pub) {
    // Match against existing peers to prevent creating multiple lobbies with the same master.
    for (const auto& [lobby_id, lobby] : lobbies) {
        for (const auto& player : lobby.players)
            if (NP_AddrEq(player.pub, pub))
                return NPE_LobbyExists; // fuck you...
    }

    lobbies.insert_or_assign({game, name}, Lobby(game, name));
    NP_Info("Created lobby '%s'", fmt_lobby_name(name));
    return NPE_Ok;
}

static void send_lobby_metadata(NP_SockAddr pub, const std::string& game, const std::string& name) {
    constexpr const size_t pnrsize = sizeof(NutPunch_LobbyName) + sizeof(NP_Metadata);
    static uint8_t buf[sizeof(NP_Header) + pnrsize] = "LGMA";

    if (!lobbies.contains({game, name}))
        return;

    const auto& lobby = lobbies.at({game, name});

    uint8_t* ptr = buf + sizeof(NP_Header);

    std::memcpy(ptr, name.data(), sizeof(NutPunch_LobbyName));
    ptr += sizeof(NutPunch_LobbyName);

    ptr += lobby.metadata.dump(ptr);

    just_send(pub, buf, ptr - buf);
}

static void send_lobbies(
    NP_SockAddr pub, const std::string& game, size_t filter_count, const NutPunch_Filter* filters) {
    constexpr const size_t fuckyou = NUTPUNCH_MAX_SEARCH_RESULTS * sizeof(NutPunch_LobbyInfo);
    static uint8_t buf[sizeof(NP_Header) + fuckyou] = "LIST"; // BITCH REFORMATS EVERY COMMIT

    if (filter_count > NUTPUNCH_MAX_SEARCH_FILTERS)
        return;

    uint8_t* ptr = buf + sizeof(NP_Header);
    size_t count = 0;

    for (const auto& [id, lobby] : lobbies) {
        if (lobby.unlisted || lobby.game != game || !lobby.match_against(filters, filter_count))
            continue;

        std::memcpy(ptr, id.name.data(), std::strlen(lobby.fmt_id()));
        ptr += sizeof(NutPunch_LobbyName);
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

struct Packet {
    const char* const header;
    void (*const handle)(Message msg);
    const size_t min_size;
};

static void handle_ligma(Message msg) {
    const auto game = msg.read0term(sizeof(NutPunch_GameId));
    const auto name = msg.read0term(sizeof(NutPunch_LobbyName));
    send_lobby_metadata(msg.from, game, name);
}

static void handle_list(Message msg) {
    const auto game = msg.read0term(sizeof(NutPunch_GameId));

    if (msg.len % sizeof(NutPunch_Filter) == 0) {
        const auto filtars = reinterpret_cast<const NutPunch_Filter*>(msg.data);
        send_lobbies(msg.from, game, msg.len / sizeof(NutPunch_Filter), filtars);
    }
}

static void handle_find(Message msg) {
    const auto peer_id = msg.read(sizeof(NutPunch_PeerId));
    const auto game_id = msg.read0term(sizeof(NutPunch_GameId));

    if (!matchmaking.contains(game_id))
        matchmaking.emplace(game_id, Grindr(game_id));

    matchmaking.at(game_id).accept(peer_id, msg.from);
}

static void handle_disc(Message msg) {
    kill_bro(msg.data, msg.from);
}

static void handle_join(Message msg) {
    const auto peer_id = msg.read(sizeof(NutPunch_PeerId));
    const auto game = msg.read0term(sizeof(NutPunch_GameId));
    const auto lobby_name = msg.read0term(sizeof(NutPunch_LobbyName));
    const auto flags = msg.read<NP_HeartbeatFlagsStorage>();

    NutPunch_ErrorCode err = NPE_Ok;

    if (lobbies.contains({game, lobby_name})) {
        if (!(flags & (NP_HB_JoinExisting | NP_HB_Queue))
            && !lobbies[{game, lobby_name}].has(peer_id))
        {
            err = NPE_LobbyExists;
        }
    } else if (flags & NP_HB_JoinExisting) {
        err = NPE_NoSuchLobby;
    } else if (lobbies.size() >= MAX_LOBBIES) {
        err = NPE_NoSuchLobby;
        NP_Warn("Reached lobby limit");
    } else {
        err = create_lobby(game, lobby_name, msg.from);
    }

    if (err != NPE_Ok) {
        gtfo(msg.from, err);
        return;
    }

    auto& lobby = lobbies.at({game, lobby_name});

    if (flags & NP_HB_Queue) // unhack the initial capacity of 1...
        lobby.capacity = Grindr::MATCH;

    lobby.accept(peer_id, flags, msg);
}

static void handle_recv(NP_SockAddr pub, const char* buf, int rcv) {
    rcv -= 4 + (int)sizeof(NP_Header);
    if (rcv < 0)
        return; // junk...

    // we completely ignore the packet id on the NutPuncher side for now
    buf += 4;

    constexpr const Packet packets[] = {
        {"LIST", handle_list,  sizeof(NutPunch_GameId)   },
        {"LGMA", handle_ligma, sizeof(NutPunch_LobbyName)},
        {"FIND", handle_find,  sizeof(NP_Find)           },
        {"DISC", handle_disc,  sizeof(NutPunch_PeerId)   },
        {"JOIN", handle_join,  sizeof(NP_Heartbeat)      },
    };

    for (const auto& packet : packets) {
        if (rcv >= packet.min_size && !memcmp(buf, packet.header, sizeof(NP_Header))) {
            packet.handle({pub, buf + sizeof(NP_Header), rcv});
            return;
        }
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
