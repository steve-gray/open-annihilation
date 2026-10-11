// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The packages' badge pictures the Library shows: asked for on the main
// thread, fetched and checked on a worker, kept on disk under the name of
// their SHA-256, and handed back decoded.
#pragma once

#include "oa/app/mod_summary.hpp"
#include "oa/base/sha256.hpp"
#include "oa/netgame/http/client.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

namespace oa::app::content {

/// The folder under the content folder that holds the pictures, each named
/// `<sha256>.png`.
inline constexpr std::string_view pictures_folder_name = "pictures";

/// The largest picture fetched or read from the folder, in bytes: a mod
/// badge's largest file.
inline constexpr uint64_t max_picture_bytes = max_mod_badge_bytes;

/// Returns the SHA-256 a picture's address names: its last segment is 64
/// hexadecimal digits and `.png`, as a catalogue's `/v1/i/<sha256>.png`.
///
/// @param url the picture's address, or a reference to it
/// @return the digest, or nothing when the last segment is not such a name
[[nodiscard]] std::optional<base::sha256::Digest> picture_digest(std::string_view url) noexcept;

/// The pictures asked for, fetched, checked and decoded.
///
/// `request`, `take` and `drop_all` are for the main thread; none reads a
/// file or the network. `fetch_waiting` is the worker's: it reads the
/// folder, fetches what the folder does not hold, and writes what it
/// fetched. A picture is used only when its bytes hash to the SHA-256 its
/// name gives, and only when it is at most max_picture_bytes. A fetch sends
/// no header of the game's own beyond the client's: no install ID.
class PictureCache {
  public:

    /// Makes an empty cache. Nothing is read yet.
    ///
    /// @param folder where the pictures are kept (`<data>/content/pictures`);
    ///     empty keeps nothing on disk
    /// @param cancel read by every fetch, set when the worker stops; null
    ///     means a fetch runs to its end
    explicit PictureCache(std::filesystem::path folder, const std::atomic<bool>* cancel = nullptr);

    /// Lets the pictures go.
    ~PictureCache();

    PictureCache(const PictureCache&) = delete;
    PictureCache& operator=(const PictureCache&) = delete;

    /// Asks for one picture. A picture already asked for, ready, or refused
    /// while this cache lives (its name names no SHA-256, its bytes hash to
    /// another, it is too large or could not be fetched) is not asked for
    /// again.
    ///
    /// @param registry the registry whose catalogue names it
    /// @param url its address, resolved against the host that served the catalogue
    void request(std::string_view registry, std::string_view url);

    /// Hands over a picture that is ready, and forgets it.
    ///
    /// @param registry the registry it was asked for under
    /// @param url its address, as asked for
    /// @return the badge, decoded and at most mod_badge_side square; nothing
    ///     while it is not ready, and once it was taken
    [[nodiscard]] std::optional<ModBadge> take(std::string_view registry, std::string_view url);

    /// Forgets every picture asked for and every one ready and not taken. The
    /// pictures refused stay refused, and the folder is left as it is.
    void drop_all();

    /// Tells whether a picture waits to be read or fetched.
    ///
    /// @return true while one does
    [[nodiscard]] bool waiting() const;

    /// Returns a count that moves on whenever a picture becomes ready.
    ///
    /// @return the count, from 0
    [[nodiscard]] uint64_t generation() const noexcept;

    /// Reads or fetches every picture that waits, one at a time, on the
    /// calling thread, which is never the game's main thread.
    ///
    /// A picture the folder holds whose bytes hash to its name is read with
    /// no request. Any other is fetched, unless a match is running: then it
    /// waits, and nothing is requested. A fetched picture whose bytes hash to
    /// its name and decode is written to the folder whole, through a file
    /// beside it renamed over it, and then is ready. Returns when nothing
    /// waits, when a match holds the rest, or when the cancel flag is set.
    ///
    /// @param http the worker's client
    /// @param match_running a match is running: nothing is fetched
    void fetch_waiting(netgame::http::Client& http, bool match_running);

  private:

    struct State;
    std::unique_ptr<State> state_;
};

} // namespace oa::app::content
