// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Small map thumbnails, decoded for the rows on screen and kept in a bounded cache.
#pragma once

#include "oa/formats/hpi.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace oa::ui::map_previews {

/// Where a thumbnail's pixels come from.
enum class PreviewSource : uint8_t {
    base,   ///< the map's terrain file, from its header and minimap
    pack,   ///< the pack's preview picture, read without mounting the pack
    online, ///< picture bytes the caller supplies
};

/// One thumbnail a row asked to show.
struct PreviewRequest {
    std::string key;            ///< map name, or the picture's catalogue path
    PreviewSource source{};     ///< where the pixels come from
    std::string map;            ///< map name; base maps name the terrain file
    std::filesystem::path file; ///< pack preview picture
};

/// One decoded thumbnail, top row first.
struct Thumbnail {
    int32_t width{};           ///< width in pixels
    int32_t height{};          ///< height in pixels
    std::vector<uint8_t> rgba; ///< width times height pixels, 4 bytes each
};

/// Reads the bytes a thumbnail is decoded from.
///
/// A null function reads nothing. `read_map` copies `size` bytes of a terrain
/// file at `offset` and returns false when fewer are there. `online_bytes`
/// returns false when those bytes are not ready yet.
struct PreviewHost {
    void* context{};
    bool (*read_map)(void* context, const char* path, uint32_t offset, void* out, uint32_t size){};
    bool (*read_file)(
        void* context, const std::filesystem::path& file, std::vector<uint8_t>* out
    ){};
    bool (*online_bytes)(void* context, const std::string& key, std::vector<uint8_t>* out){};
    const oa::PaletteBytes* palette{}; ///< 256 colours, 4 bytes each; base maps need it
};

/// A bounded cache of map thumbnails.
///
/// ask records a visible row. pump decodes a few of those rows. new_frame
/// drops rows that were not asked for again, so a map nobody is looking at
/// is not decoded. The cache is used from one thread.
class PreviewCache {
  public:

    /// Makes a cache that keeps at most `most_entries` thumbnails.
    ///
    /// `side` is the square a thumbnail is fitted into, in pixels, kept
    /// within 1 and the largest picture box a map preview fills. A cache of
    /// no entries decodes and then drops each thumbnail.
    ///
    /// @param most_entries how many thumbnails the cache keeps
    /// @param side the square a thumbnail is fitted into, in pixels
    explicit PreviewCache(std::size_t most_entries = 256, int32_t side = 128);

    PreviewCache(const PreviewCache&) = delete;
    PreviewCache& operator=(const PreviewCache&) = delete;

    /// Returns a thumbnail already decoded for this key, or records the request.
    ///
    /// A key that is still waiting, or whose decode failed, returns null.
    /// Asking again moves that waiting request ahead of older ones. The
    /// pointer stays valid until pump drops that thumbnail.
    ///
    /// @param request the map or picture to show
    /// @return the thumbnail, or null when it is not decoded
    const Thumbnail* ask(const PreviewRequest& request);

    /// Tells whether a thumbnail was asked for and could not be decoded.
    ///
    /// A failed key is not decoded again. An online picture whose bytes are
    /// not ready yet has not failed.
    ///
    /// @param key the request's key
    /// @return true when decoding that key failed
    bool failed(std::string_view key) const;

    /// Decodes up to a few of the waiting requests, the most recently asked first.
    ///
    /// An online request whose bytes are not ready stays waiting and does not
    /// count against the limit. A decode that fails is remembered. When the
    /// cache is full, the least recently asked thumbnail is dropped.
    ///
    /// @param host reads the picture's bytes and supplies the game palette
    /// @param most_decodes how many requests to decode this call; 0 decodes none
    void pump(const PreviewHost& host, int32_t most_decodes = 4);

    /// Forgets waiting requests that were not asked again since the last frame.
    ///
    /// Decoded thumbnails stay. A row that is no longer on screen is not decoded.
    void new_frame();

  private:

    struct Cached {
        std::string key;
        Thumbnail thumbnail;
        std::uint64_t asked{};
    };

    struct Waiting {
        PreviewRequest request;
        std::uint64_t asked{};
    };

    /// Drops the least recently asked thumbnail.
    void drop_oldest();

    /// Keeps a decoded thumbnail, dropping the least recently asked one when full.
    ///
    /// @param key the request's key
    /// @param thumbnail the decoded pixels
    /// @param asked how recently the row asked, larger asked more recently
    void store(std::string key, Thumbnail thumbnail, std::uint64_t asked);

    std::size_t most_entries_;
    int32_t side_;
    std::uint64_t stamp_ = 0;
    // Front is not special: asked says which thumbnail is oldest.
    std::list<Cached> cached_;
    std::unordered_map<std::string, std::list<Cached>::iterator> by_key_;
    std::vector<Waiting> waiting_;
    std::unordered_set<std::string> failed_;
};

} // namespace oa::ui::map_previews
