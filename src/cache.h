#pragma once

#include "wire.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

struct CacheEntry {
    Frame       payload;    // msgpack bytes
    std::string publisher;  // inferred from source socket
    uint64_t    sequence;
    uint64_t    timestamp;
};

using CacheSnapshot = std::vector<std::pair<std::string, CacheEntry>>;

class Cache {
public:
    // Insert or overwrite a property (last-write-wins)
    void set(const std::string &path, Frame payload,
             const std::string &publisher, uint64_t sequence, uint64_t timestamp);

    // Exact lookup — returns nullptr if not found
    const CacheEntry *get(const std::string &path) const;

    // Returns all entries whose path matches the given domain prefix.
    // Empty prefix matches everything (used by subscribe_all / snapshot wildcard).
    CacheSnapshot get_prefix(const std::string &prefix) const;

    size_t size() const { return entries_.size(); }
    void   clear()      { entries_.clear(); }

private:
    std::unordered_map<std::string, CacheEntry> entries_;
};
