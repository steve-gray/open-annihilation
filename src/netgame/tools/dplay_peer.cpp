// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A DirectPlay peer run by hand: hosts or joins a TA session and prints
// every system message and the record types of every condenser frame it
// receives. Interrupting it (Ctrl-C) leaves the session as a closing game
// does, so the other machines see its players leave; a joined peer whose
// host leaves that way may take its place, and then answers searches and
// takes new machines in.
//
//   oa-netgame-dplay-peer [--dplay-guid GUID] host [name]
//   oa-netgame-dplay-peer [--dplay-guid GUID] join [ipv4]
//
// --dplay-guid is the application GUID to host under and to look for,
// with or without braces. Without it the peer uses Total Annihilation's.

#include "oa/netgame/frame.hpp"
#include "oa/netgame/session.hpp"
#include "oa/netgame/socket_host.hpp"
#include "oa/netgame/network.hpp"

#include <charconv>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <vector>

using namespace oa::netgame;

namespace {

/// Set once the peer is interrupted; the receive loop then leaves the session.
volatile std::sig_atomic_t g_interrupted = 0;

/// Marks the peer interrupted, as the SIGINT and SIGTERM handler.
void on_interrupt(int) {
    g_interrupted = 1;
}

bool parse_ip(const char* text, uint8_t out[4]) {
    const char* at = text;
    const char* const end = text + std::strlen(text);
    for (int index = 0; index < 4; ++index) {
        if (index > 0) {
            if (at == end || *at != '.')
                return false;
            ++at;
        }
        unsigned value = 0;
        const auto [next, error] = std::from_chars(at, end, value);
        if (error != std::errc{} || value > 255)
            return false;
        out[index] = static_cast<uint8_t>(value);
        at = next;
    }
    return true;
}

/// Prints a system message's type and the player it concerns.
///
/// A created player shows its id, name and join data; a renamed player its
/// id and names; a player whose data changed its id and data size; this
/// machine becoming the session's host says so; another message shows the
/// u32 at byte 8.
///
/// @param bytes Message bytes.
/// @param size Message length in bytes.
void print_system(const uint8_t* bytes, uint32_t size) {
    const uint32_t type = size >= 4 ? load_u32(bytes) : 0;
    std::printf("system 0x%04x", type);
    dplay::CreatePlayerView view{};
    dplay::PlayerNameView name{};
    dplay::PlayerDataView data{};
    if (type == static_cast<uint32_t>(SystemMessageType::host_changed)) {
        std::printf(" this machine now hosts the session");
    } else if (dplay::decode_player_name_image(bytes, size, &name)) {
        std::printf(
            " player 0x%08x renamed '%s' '%s'",
            name.id,
            name.short_name ? name.short_name : "",
            name.long_name ? name.long_name : ""
        );
    } else if (dplay::decode_player_data_image(bytes, size, &data)) {
        std::printf(" player 0x%08x data %u bytes", data.id, data.data_size);
    } else if (dplay::decode_create_player_image(bytes, size, &view)) {
        std::printf(
            " player 0x%08x '%s' data %u bytes",
            view.id,
            view.short_name ? view.short_name : "",
            view.data_size
        );
        CreatePlayerData block{};
        if (view.data != nullptr &&
            decode_create_player_data(view.data, view.data_size, &block) == WireError::ok)
            std::printf(" tag '%.16s' u16 %u %u", block.tag, block.version_low, block.version_high);
    } else if (size >= 12) {
        std::printf(" id 0x%08x", load_u32(bytes + 8));
    }
    std::printf("\n");
}

void print_frame(uint32_t from, uint32_t to, const uint8_t* bytes, uint32_t size) {
    std::printf("data 0x%08x -> 0x%08x %u bytes", from, to, size);
    try {
        const auto frame = oa::netgame::network::decode_frame({bytes, size});
        if (frame.size() >= frame_header_bytes) {
            std::printf(" frame %d:", static_cast<int32_t>(load_u32(frame.data())));
            std::size_t at = frame_header_bytes;
            uint16_t length = 0;
            while (at < frame.size() &&
                   record_wire_length(frame.data() + at, frame.size() - at, &length) ==
                       WireError::ok) {
                std::printf(" %02x", frame[at]);
                at += length;
            }
        }
    } catch (const std::exception& e) {
        std::printf(" (not a condenser frame: %s)", e.what());
    }
    std::printf("\n");
}

/// Prints how to run the peer.
///
/// @param program The program's name, as argv[0].
void print_usage(const char* program) {
    std::fprintf(stderr, "usage: %s [--dplay-guid GUID] host [name] | join [ipv4]\n", program);
}

/// Reads host or join, an optional address or name, and --dplay-guid.
///
/// A missing mode prints the usage. A GUID that is not eight-four-four-four-twelve
/// hexadecimal digits, with or without braces, is refused. Without --dplay-guid
/// the application GUID is Total Annihilation's.
///
/// @param argc Argument count.
/// @param argv Arguments; argv[0] is the program name.
/// @param[out] hosting True when the peer hosts.
/// @param[out] extra The session name or the address, or null when omitted.
/// @param[out] application The application GUID to host under and to look for.
/// @return False when the arguments are refused; a message is already printed.
bool read_arguments(
    int argc, char** argv, bool& hosting, const char*& extra, dplay::Guid& application
) {
    std::memcpy(application.bytes, application_guid, 16);
    std::vector<const char*> positional;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--dplay-guid") == 0) {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "--dplay-guid requires a value\n");
                return false;
            }
            ++index;
            if (!parse_application_guid(argv[index], application.bytes)) {
                std::fprintf(stderr, "--dplay-guid expects a GUID, with or without braces\n");
                return false;
            }
            continue;
        }
        positional.push_back(argv[index]);
    }
    if (positional.empty() ||
        (std::strcmp(positional[0], "host") != 0 && std::strcmp(positional[0], "join") != 0)) {
        print_usage(argv[0]);
        return false;
    }
    if (positional.size() > 2) {
        print_usage(argv[0]);
        return false;
    }
    hosting = std::strcmp(positional[0], "host") == 0;
    extra = positional.size() == 2 ? positional[1] : nullptr;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    bool hosting = false;
    const char* extra = nullptr;
    dplay::Guid app{};
    if (!read_arguments(argc, argv, hosting, extra, app))
        return 2;
    auto machine = std::make_unique<sock::Host>();
    sock::HostConfig config{};
    if (!hosting && extra != nullptr && !parse_ip(extra, config.enum_target)) {
        std::fprintf(stderr, "bad address %s\n", extra);
        return 2;
    }
    if (!sock::host_open(machine.get(), config)) {
        std::fprintf(stderr, "open failed: %s\n", machine->error);
        return 1;
    }
    auto session = std::make_unique<session::Session>();
    session::session_init_multiplay(session.get(), sock::host_session_backend(machine.get()), app);
    session::session_init_defaults(session.get());
    uint32_t player = 0;
    if (hosting) {
        if (!session::session_create_game(
                session.get(), extra != nullptr ? extra : "oa peer", "", 0, 0, 0, 0
            )) {
            std::fprintf(stderr, "create failed: %s\n", machine->error);
            return 1;
        }
        std::printf(
            "hosting on stream %u datagram %u\n",
            machine->engine.config.stream.port,
            machine->engine.config.datagram.port
        );
    } else {
        session::GameEntry games[session::max_game_entries]{};
        const int32_t count =
            session::session_get_games(session.get(), games, session::max_game_entries);
        std::printf("%d game(s)\n", count);
        for (int32_t i = 0; i < count; ++i)
            std::printf("  '%s' max %u\n", games[i].session_name, games[i].max_players);
        if (count <= 0) {
            std::fprintf(stderr, "no games found\n");
            return 1;
        }
        if (!session::session_join_game(session.get(), games[0].instance)) {
            const char* name = dplay::result_name(session::session_last_join_result(session.get()));
            std::fprintf(
                stderr, "join failed: %s\n", name != nullptr ? name : "undocumented result"
            );
            return 1;
        }
        std::printf("joined '%s'\n", session->session_name);
    }
    if (!session::session_add_player(
            session.get(), &player, "peer", "peer", "", 0, create_player_version_high
        )) {
        std::fprintf(stderr, "add player failed\n");
        return 1;
    }
    std::printf("local player 0x%08x\n", player);
    std::signal(SIGINT, on_interrupt);
    std::signal(SIGTERM, on_interrupt);
    static uint8_t buffer[0x10000];
    while (g_interrupted == 0) {
        uint32_t size = sizeof buffer;
        const uint32_t r = session::session_receive(session.get(), buffer, &size);
        if (r == dplay::result::no_messages) {
            sock::host_pump(machine.get(), 50);
            continue;
        }
        if (r != dplay::result::ok)
            continue;
        if (session->recv_from == system_message_sender_id)
            print_system(buffer, size);
        else
            print_frame(session->recv_from, session->recv_to, buffer, size);
        std::fflush(stdout);
    }
    (void)session::session_quit_game(session.get());
    sock::host_close(machine.get());
    std::printf("left the session\n");
    return 0;
}
