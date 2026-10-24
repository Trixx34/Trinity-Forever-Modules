/*
 * Bot ownership rules. See ModuleBotOwnership.h.
 */

#include "ModuleBotOwnership.h"

#include <algorithm>

ModuleBotOwnership::Claim ModuleBotOwnership::TryClaim(uint64 botGuid, uint32 module, uint32 maxBots, uint32& owner)
{
    auto it = _owners.find(botGuid);
    if (it != _owners.end())
    {
        owner = it->second;
        return owner == module ? Claim::Granted : Claim::OwnedByOther;
    }

    if (!CanTakeMore(module, maxBots))
    {
        owner = 0;
        return Claim::LimitReached;
    }

    _owners.emplace(botGuid, module);
    owner = module;
    return Claim::Granted;
}

bool ModuleBotOwnership::CanTakeMore(uint32 module, uint32 maxBots) const
{
    return OwnedCount(module) + ReservedCount(module) < maxBots;
}

bool ModuleBotOwnership::TryReserve(uint32 module, uint32 maxBots)
{
    if (!CanTakeMore(module, maxBots))
        return false;
    ++_reserved[module];
    return true;
}

void ModuleBotOwnership::CancelReserve(uint32 module)
{
    auto it = _reserved.find(module);
    if (it == _reserved.end() || it->second == 0)
        return;
    if (--it->second == 0)
        _reserved.erase(it);
}

ModuleBotOwnership::Claim ModuleBotOwnership::CommitReserve(uint64 botGuid, uint32 module, uint32& owner)
{
    auto reserved = _reserved.find(module);
    if (reserved == _reserved.end() || reserved->second == 0)
    {
        owner = 0;
        return Claim::LimitReached;
    }
    if (--reserved->second == 0)
        _reserved.erase(reserved);

    auto it = _owners.find(botGuid);
    if (it != _owners.end())
    {
        owner = it->second;
        return owner == module ? Claim::Granted : Claim::OwnedByOther;
    }
    _owners.emplace(botGuid, module);
    owner = module;
    return Claim::Granted;
}

uint32 ModuleBotOwnership::ReservedCount(uint32 module) const
{
    auto it = _reserved.find(module);
    return it == _reserved.end() ? 0 : it->second;
}

bool ModuleBotOwnership::Release(uint64 botGuid, uint32 module)
{
    auto it = _owners.find(botGuid);
    if (it == _owners.end() || it->second != module)
        return false;
    _owners.erase(it);
    return true;
}

bool ModuleBotOwnership::Revoke(uint64 botGuid, uint32& previousOwner)
{
    auto it = _owners.find(botGuid);
    if (it == _owners.end())
        return false;
    previousOwner = it->second;
    _owners.erase(it);
    return true;
}

std::vector<uint64> ModuleBotOwnership::DropModule(uint32 module)
{
    std::vector<uint64> released;
    for (auto it = _owners.begin(); it != _owners.end();)
    {
        if (it->second == module)
        {
            released.push_back(it->first);
            it = _owners.erase(it);
        }
        else
            ++it;
    }
    _reserved.erase(module);
    return released;
}

std::vector<uint64> ModuleBotOwnership::BotsOf(uint32 module) const
{
    std::vector<uint64> bots;
    for (auto const& entry : _owners)
        if (entry.second == module)
            bots.push_back(entry.first);
    std::sort(bots.begin(), bots.end());
    return bots;
}

uint32 ModuleBotOwnership::OwnedCount(uint32 module) const
{
    uint32 count = 0;
    for (auto const& entry : _owners)
        if (entry.second == module)
            ++count;
    return count;
}

bool ModuleBotOwnership::Owner(uint64 botGuid, uint32& owner) const
{
    auto it = _owners.find(botGuid);
    if (it == _owners.end())
        return false;
    owner = it->second;
    return true;
}
