#include "ModuleStoreRules.h"

#include <atomic>

namespace ModuleStore
{

namespace
{
std::atomic<std::size_t> g_maxRows{DefaultMaxRowsPerModule};
}

std::size_t ClampMaxRows(long long configured)
{
    if (configured < static_cast<long long>(MinRowsPerModule))
        return MinRowsPerModule;
    if (configured > static_cast<long long>(HardMaxRowsPerModule))
        return HardMaxRowsPerModule;
    return static_cast<std::size_t>(configured);
}

void SetMaxRowsPerModule(std::size_t limit)
{
    g_maxRows = ClampMaxRows(static_cast<long long>(limit));
}

std::size_t MaxRowsPerModule()
{
    return g_maxRows;
}

bool ListHasRoom(std::size_t usedBytes, std::size_t keyBytes)
{
    return usedBytes + keyBytes + ListEntryOverhead <= MaxListBytes;
}

bool ValidKey(std::string const& key, std::string& reason)
{
    if (key.empty() || key.size() > MaxKeyBytes)
    {
        reason = "key must be 1 to " + std::to_string(MaxKeyBytes) + " characters";
        return false;
    }
    for (unsigned char c : key)
    {
        if (c <= 0x20 || c >= 0x7F)
        {
            reason = "key must be printable ASCII without spaces";
            return false;
        }
    }
    return true;
}

std::string PrefixPattern(std::string const& prefix)
{
    std::string pattern;
    for (char c : prefix)
    {
        if (c == '%' || c == '_' || c == '\\')
            pattern += '\\';
        pattern += c;
    }
    return pattern + "%";
}

} // namespace ModuleStore
