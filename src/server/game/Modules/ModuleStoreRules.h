/*
 * Limits and key rules for the module store (doc/module-system/15-module-store.md). No database dependencies, so the
 * rules are unit-tested directly (tests/ModuleStoreRulesTest.cpp). ModuleStore applies them before it touches the database.
 */

#ifndef TRINITYCORE_MODULE_STORE_RULES_H
#define TRINITYCORE_MODULE_STORE_RULES_H

#include <cstddef>
#include <string>

namespace ModuleStore
{
inline constexpr std::size_t MaxKeyBytes = 128;
inline constexpr std::size_t MaxValueBytes = 1024;
// Rows per module: Modules.Store.MaxRowsPerModule, clamped to [MinRowsPerModule, HardMaxRowsPerModule]. Rows are at most
// about 1.2 KiB, so the hard bound is 20 MiB of database rows per module.
inline constexpr std::size_t DefaultMaxRowsPerModule = 5000;
inline constexpr std::size_t MinRowsPerModule = 100;
inline constexpr std::size_t HardMaxRowsPerModule = 20000;
// A list reply must fit one wire frame (ModuleWire::MaxFrameBytes, 1 MiB): keys are added until this many bytes
// (key length plus ListEntryOverhead each) are used; the rest are cut off and the module is told in the log.
inline constexpr std::size_t MaxListBytes = 900 * 1024;
inline constexpr std::size_t ListEntryOverhead = 8;

// The configured limit brought into [MinRowsPerModule, HardMaxRowsPerModule].
std::size_t ClampMaxRows(long long configured);
// The limit in force (DefaultMaxRowsPerModule until ModuleMgr sets it from the config). Also the most keys a list returns.
void SetMaxRowsPerModule(std::size_t limit);
std::size_t MaxRowsPerModule();
// True if one more key of this length still fits the list byte budget after `usedBytes`.
bool ListHasRoom(std::size_t usedBytes, std::size_t keyBytes);

// Keys are printable ASCII without spaces, 1 to MaxKeyBytes long. Returns false, with the reason, if one is not.
bool ValidKey(std::string const& key, std::string& reason);

// The LIKE pattern that matches keys starting with `prefix`: %, _ and \ in the prefix are escaped.
std::string PrefixPattern(std::string const& prefix);
} // namespace ModuleStore

#endif // TRINITYCORE_MODULE_STORE_RULES_H
