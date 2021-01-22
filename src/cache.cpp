#include "cache.h"

void Cache::set(const std::string &path, Frame payload,
                const std::string &publisher, uint64_t sequence, uint64_t timestamp)
{
    entries_[path] = { std::move(payload), publisher, sequence, timestamp };
}

const CacheEntry *Cache::get(const std::string &path) const
{
    auto it = entries_.find(path);
    if (it == entries_.end())
        return nullptr;
    return &it->second;
}

CacheSnapshot Cache::get_prefix(const std::string &prefix) const
{
    CacheSnapshot result;

    for (const auto &[path, entry] : entries_) {
        if (prefix.empty()
            || path == prefix
            || (path.size() > prefix.size()
                && path[prefix.size()] == '.'
                && path.compare(0, prefix.size(), prefix) == 0))
        {
            result.emplace_back(path, entry);
        }
    }

    return result;
}
