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
#include <map>
#include <string>

constexpr const uint64_t BEATS_PER_SECOND = 60, KEEP_ALIVE_SECONDS = 5, MAX_LOBBIES = 512;

struct Lobby;
static NP_Socket sock = NUTPUNCH_INVALID_SOCKET;
static std::map<std::string, Lobby> lobbies;

template <typename T> static bool is_memzero(const T& x) {
    static constexpr const char zero[sizeof(T)] = {0};
    return !std::memcmp((char*)&x, zero, sizeof(zero));
}

static const char* fmt_lobby_id(const char* id) {
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

struct Field : NutPunch_Field {
    Field() {
        reset();
    }

    explicit operator bool() const {
        return size && !is_memzero(name);
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

struct AddrInfo : NP_AddrInfo {
    AddrInfo() {
        reset();
    }

    void reset() {
        std::memset(this, 0, sizeof(*this));
    }

    template <typename T, typename Size> int send(const T buf[], Size size) const {
        const auto* cbuf = reinterpret_cast<const char*>(buf);
        const auto* csock = reinterpret_cast<const sockaddr*>(this);
        const auto csize = static_cast<int>(size);
        return sendto(sock, cbuf, csize, 0, csock, sizeof(*this));
    }

    void gtfo(uint8_t error) const {
        static uint8_t buf[sizeof(NP_Header) + 1] = "GTFO";
        buf[sizeof(NP_Header)] = error;
        for (int i = 0; i < 5; i++)
            send(buf, sizeof(buf));
    }

    void dump(uint8_t* ptr) const {
        std::memcpy(ptr, &sin_addr, 4), ptr += 4;
        std::memcpy(ptr, &sin_port, 2), ptr += 2;
    }

    void load(uint8_t* ptr) {
        std::memcpy(&sin_addr, ptr, 4), ptr += 4;
        std::memcpy(&sin_port, ptr, 2), ptr += 2;
    }
};

static bool operator==(const NP_AddrInfo& a, const NP_AddrInfo& b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

struct Player {
    NutPunch_PeerId id;
    AddrInfo pub, internal;
    std::uint32_t countdown = 0;

    Player() {}

    explicit operator bool() const {
        return countdown > 0 && !is_memzero(id);
    }

    void reset() {
        std::memset(id, 0, sizeof(id));
        pub.reset(), internal.reset(), countdown = 0;
    }
};

struct Lobby {
    NutPunch_LobbyId id = {0};
    bool unlisted = false;
    uint8_t capacity = NUTPUNCH_MAX_PLAYERS;
    Player players[NUTPUNCH_MAX_PLAYERS] = {};
    Metadata metadata;

    Lobby() {}

    Lobby(const std::string& id) {
        std::memcpy(this->id, id.c_str(), sizeof(this->id));
    }

    const char* fmt_id() const {
        return fmt_lobby_id(id);
    }

    void update() {
        for (NutPunch_Peer i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
            if (players[i])
                tick(i);
            if (players[i])
                beat(i);
        }
    }

    int special(uint8_t idx) const {
        switch (idx) {
        case NPSF_Capacity:
            return capacity;
        case NPSF_Players:
            return gamers();
        default:
            return 0;
        }
    }

    explicit operator bool() const {
        return gamers() > 0;
    }

    NutPunch_Peer index_of(const NutPunch_PeerId id) const {
        for (NutPunch_Peer i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
            if (players[i] && !std::memcmp(id, players[i].id, sizeof(NutPunch_PeerId)))
                return i;
        return NUTPUNCH_MAX_PLAYERS;
    }

    NutPunch_Peer next_dead() const {
        for (NutPunch_Peer i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
            if (!players[i])
                return i;
        return NUTPUNCH_MAX_PLAYERS;
    }

    bool has(const NutPunch_PeerId id) const {
        return index_of(id) != NUTPUNCH_MAX_PLAYERS;
    }

    NutPunch_Peer gamers() const {
        NutPunch_Peer count = 0;
        for (NutPunch_Peer i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
            if (players[i])
                count++;
        return count;
    }

    void accept(const NutPunch_PeerId id, const AddrInfo& pub, const AddrInfo& internal,
        const NP_HeartbeatFlagsStorage flags, const char* meta) {
        NutPunch_Peer idx = index_of(id);
        bool just_joined = false;

        if (idx == NUTPUNCH_MAX_PLAYERS)
            idx = next_dead(), just_joined = true;

        if (idx == NUTPUNCH_MAX_PLAYERS) {
            pub.gtfo(NPE_LobbyFull);
            return;
        }

        if (just_joined)
            NP_Info("Player %d joined lobby '%s'", idx + 1, fmt_id());

        auto& player = players[idx];
        std::memcpy(player.id, id, sizeof(NutPunch_PeerId));
        player.pub = pub, player.internal = internal;
        player.countdown = KEEP_ALIVE_SECONDS * BEATS_PER_SECOND;

        if (idx == master()) {
            unlisted = flags & NP_HB_Unlisted;
            capacity = 1 + (flags >> 4);
            metadata.load(meta);
        }
    }

    void tick(const NutPunch_Peer idx) {
        auto& plr = players[idx];
        plr.countdown--;
        if (plr)
            return;
        NP_Warn("Player %d timed out", idx + 1);
        plr.reset();
    }

    void beat(const NutPunch_Peer idx) {
        static uint8_t buf[sizeof(NP_Header) + sizeof(NP_Beating)] = "BEAT";
        uint8_t* ptr = buf + sizeof(NP_Header);

        *ptr++ = unlisted;
        *ptr++ = idx;
        *ptr++ = master();
        *ptr++ = capacity;

        for (NutPunch_Peer i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
            players[i].pub.dump(ptr);
            ptr += sizeof(NP_PeerAddr);

            players[i].internal.dump(ptr);
            ptr += sizeof(NP_PeerAddr);
        }

        std::memcpy(ptr, &metadata, sizeof(Metadata));
        ptr += sizeof(Metadata);

        auto& player = players[idx];
        if (player.pub.send(buf, ptr - buf) <= 0) {
            NP_Warn("Player %d lost connection", idx + 1);
            player.reset();
        }
    }

    bool kill_bro(const NutPunch_PeerId id) {
        for (NutPunch_Peer i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
            if (!players[i] || std::memcmp(players[i].id, id, sizeof(NutPunch_PeerId)))
                continue;
            NP_Info("Player %d disconnected gracefully", i + 1);
            players[i].reset();
            return true;
        }
        return false;
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
        if (master_idx < NUTPUNCH_MAX_PLAYERS && players[master_idx])
            return master_idx;
        for (master_idx = 0; master_idx < NUTPUNCH_MAX_PLAYERS; master_idx++)
            if (players[master_idx])
                break;
        return master_idx;
    }

  private:
    NutPunch_Peer master_idx = NUTPUNCH_MAX_PLAYERS;
};

static bool bind_sock() {
    AddrInfo addr;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == NUTPUNCH_INVALID_SOCKET) {
        const int err = NP_SockError();
        NP_Warn("Failed to create the underlying UDP socket (%d)", err);
        return false;
    }

    if (!NP_MakeReuseAddr(sock)) {
        const int err = NP_SockError();
        NP_Warn("Failed to set socket reuseaddr option (%d)", err);
        goto sockfail;
    }

    if (!NP_MakeNonblocking(sock)) {
        const int err = NP_SockError();
        NP_Warn("Failed to set socket to non-blocking mode (%d)", err);
        goto sockfail;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(NUTPUNCH_SERVER_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (!bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))) {
        NP_Info("Bound to port %d", NUTPUNCH_SERVER_PORT);
        return true;
    }

    NP_Warn("Failed to bind an IPv4 socket (%d)", NP_SockError());
sockfail:
    NP_NukeSocket(&sock);
    return false;
}

static bool create_lobby(const char* id, const AddrInfo& pub) {
    if (lobbies.size() >= MAX_LOBBIES) {
        pub.gtfo(NPE_NoSuchLobby);
        NP_Warn("Reached lobby limit");
        return false;
    }

    // Match against existing peers to prevent creating multiple lobbies
    // with the same master.
    for (const auto& [lobby_id, lobby] : lobbies) {
        for (const auto& player : lobby.players)
            if (player && player.pub == pub)
                return true; // fuck you...
    }

    lobbies.insert({id, Lobby(id)});
    NP_Info("Created lobby '%s'", fmt_lobby_id(id));
    return true;
}

static void send_lobbies(AddrInfo addr, const char* target_id) {
    static uint8_t buf[sizeof(NP_Header) + sizeof(NutPunch_LobbyId) + sizeof(NutPunch_Metadata)]
        = "LGMA";
    uint8_t* ptr = buf + sizeof(NP_Header);

    std::memcpy(ptr, target_id, sizeof(NutPunch_LobbyId));
    ptr += sizeof(NutPunch_LobbyId);

    for (const auto& [id, lobby] : lobbies) {
        if (id != target_id || lobby.unlisted)
            continue;

        for (const auto& field : lobby.metadata.fields) {
            if (is_memzero(field))
                continue;
            std::memcpy(ptr, &field, sizeof(NutPunch_Field));
            ptr += sizeof(NutPunch_Field);
        }

        addr.send(buf, ptr - buf);
        break;
    }
}

static void send_lobbies(AddrInfo addr, size_t filter_count, const NutPunch_Filter* filters) {
    static uint8_t
        buf[sizeof(NP_Header) + (NUTPUNCH_MAX_SEARCH_RESULTS * sizeof(NutPunch_LobbyInfo))]
        = "LIST";
    uint8_t* ptr = buf + sizeof(NP_Header);

    size_t count = 0;

    for (const auto& [id, lobby] : lobbies) {
        if (lobby.unlisted || !lobby.match_against(filters, filter_count))
            continue;

        std::memcpy(ptr, id.data(), std::strlen(lobby.fmt_id()));
        ptr += sizeof(NutPunch_LobbyId);
        *ptr++ = lobby.gamers(), *ptr++ = lobby.capacity;

        if (++count >= NUTPUNCH_MAX_SEARCH_RESULTS)
            break;
    }

    addr.send(buf, ptr - buf);
}

static void kill_bro(const NutPunch_PeerId bro) {
    for (auto& [id, lobby] : lobbies)
        if (lobby.kill_bro(bro))
            break;
}

constexpr const int RecvKeepGoing = 0, RecvDone = -1;

static int receive() {
    if (sock == NUTPUNCH_INVALID_SOCKET)
        return RecvDone;

    char heartbeat[NUTPUNCH_BUFFER_SIZE] = {0};

    AddrInfo pub;
    auto* c_addr = reinterpret_cast<sockaddr*>(&pub);
    socklen_t addr_size = sizeof(pub);

    int rcv = recvfrom(sock, heartbeat, sizeof(heartbeat), 0, c_addr, &addr_size);
    if (rcv < 0) {
        switch (NP_SockError()) {
        case NP_TooFat:
            NP_Trace("FAT PACKET");
        case NP_ConnReset:
            return RecvKeepGoing;
        case NP_WouldBlock:
            return RecvDone;
        default:
            const int err = NP_SockError();
            NP_Trace("RECV ERROR %d", err);
            return err;
        }
    }

    rcv -= sizeof(NP_Header);
    if (rcv < 0)
        return RecvKeepGoing; // junk...

    const char* ptr = heartbeat + sizeof(NP_Header);

    if (!std::memcmp(heartbeat, "LIST", sizeof(NP_Header))) {
        if (rcv == sizeof(NutPunch_LobbyId)) {
            send_lobbies(pub, ptr);
        } else if (!(rcv % sizeof(NutPunch_Filter))) {
            send_lobbies(
                pub, rcv / sizeof(NutPunch_Filter), reinterpret_cast<const NutPunch_Filter*>(ptr));
        } else {
            // junk...
        }

        return RecvKeepGoing;
    }

    if (rcv >= sizeof(NutPunch_PeerId) && !std::memcmp(heartbeat, "DISC", sizeof(NP_Header))) {
        kill_bro(ptr);
        return RecvKeepGoing;
    }

    if (rcv != sizeof(NP_Heartbeat))
        return RecvKeepGoing; // most likely junk...
    if (std::memcmp(heartbeat, "JOIN", sizeof(NP_Header)))
        return RecvKeepGoing;

    static NutPunch_PeerId peer_id = {0};
    std::memcpy(peer_id, ptr, sizeof(NutPunch_PeerId));
    ptr += sizeof(NutPunch_PeerId);

    static char lobby_id[sizeof(NutPunch_LobbyId) + 1] = {0};
    std::memcpy(lobby_id, ptr, sizeof(NutPunch_LobbyId));
    ptr += sizeof(NutPunch_LobbyId);

    AddrInfo internal;
    internal.load((uint8_t*)ptr);
    ptr += sizeof(NP_PeerAddr);

    auto flags = *(const NP_HeartbeatFlagsStorage*)ptr;
    ptr += sizeof(flags);

    NutPunch_ErrorCode err = NPE_Ok;

    if (lobbies.count(lobby_id)) {
        if (!(flags & NP_HB_JoinExisting) && !lobbies[lobby_id].has(peer_id))
            err = NPE_LobbyExists;
    } else if (flags & NP_HB_JoinExisting) {
        err = NPE_NoSuchLobby;
    } else if (!create_lobby(lobby_id, pub)) {
        return RecvKeepGoing;
    }

    if (err == NPE_Ok)
        lobbies[lobby_id].accept(peer_id, pub, internal, flags, ptr);
    else
        pub.gtfo(err);

    return RecvKeepGoing;
}

struct Soque {
    Soque() {
#ifdef NUTPUNCH_WINDOSE
        WSADATA _bitch = {0};
        WSAStartup(MAKEWORD(2, 2), &_bitch);
#endif
    }

    ~Soque() {
        NP_NukeSocket(&sock);
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

    Soque _init;

    if (!bind_sock())
        return EXIT_FAILURE;

    constexpr const NutPunch_Clock MIN_DELTA = NUTPUNCH_NS / BEATS_PER_SECOND;
    int result = 0;

    NP_Info("Running on port %d", NUTPUNCH_SERVER_PORT);
    for (;;) {
        const NutPunch_Clock start = NutPunch_TimeNS();

        if (sock == NUTPUNCH_INVALID_SOCKET) {
            NP_Warn("SOCKET DIED!!!");
            return EXIT_FAILURE;
        }

        do { result = receive(); } while (result == RecvKeepGoing);

        if (result > 0) {
            NP_Warn("Failed to receive data (code %d)", result);
            sock = NUTPUNCH_INVALID_SOCKET;
        }

        for (auto& [id, lobby] : lobbies)
            lobby.update();

        std::erase_if(lobbies, [](const auto& kv) {
            const auto& lobby = kv.second;
            if (!lobby)
                NP_Info("Deleting lobby '%s'", lobby.fmt_id());
            return !lobby;
        });

        const NutPunch_Clock delta = NutPunch_TimeNS() - start, diff = MIN_DELTA - delta;
        if (diff > 0)
            NP_SleepMs(diff / NUTPUNCH_MS);
    }

    return EXIT_SUCCESS;
}
