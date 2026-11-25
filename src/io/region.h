// Region files: 32x32 chunks per file, zlib-compressed, with a lookup table at the start.
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <cstdint>

struct Chunk;

class RegionStore {
public:
    explicit RegionStore(const std::string& worldDir);
    // Thread-safe. Fills `c` (blocks/meta/tiles/entities) if the chunk was saved before.
    bool load(int cx, int cz, Chunk& c);
    // Main thread: serialise a modified chunk into the pending list (written on flush)
    void store(const Chunk& c);
    // Write every pending chunk to its region file (temp file + rename)
    void flush();
private:
    std::string dir;
    std::mutex m;
    std::unordered_map<int64_t, std::vector<uint8_t>> pending;  // compressed chunk blobs
    bool readBlob(int cx, int cz, std::vector<uint8_t>& out);
    std::string regionPath(int rx, int rz) const;
};

// Chunk (de)serialisation helpers
void serializeChunk(const Chunk& c, std::vector<uint8_t>& out);
bool deserializeChunk(const std::vector<uint8_t>& in, Chunk& c);
bool zcompress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out);
bool zdecompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out);
