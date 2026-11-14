/*
 * Bot names: the names file parser and the picker. See BotNames.h.
 */

#include "BotNames.h"

#include <algorithm>
#include <charconv>

namespace BotNames
{
namespace
{
bool IsAsciiLetter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

std::string_view Trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n'))
        text.remove_suffix(1);
    return text;
}

std::vector<std::string_view> Split(std::string_view text, char separator)
{
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true)
    {
        size_t const end = text.find(separator, start);
        parts.push_back(Trim(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start)));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return parts;
}

bool ParseUInt(std::string_view text, uint32& out)
{
    if (text.empty())
        return false;
    auto const [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    return error == std::errc() && end == text.data() + text.size();
}

// The race and class ids of the doc (the Classic races and classes).
bool IsKnownRace(uint32 race) { return (race >= 1 && race <= 8) || race == 10 || race == 11; }
bool IsKnownClass(uint32 classId) { return (classId >= 1 && classId <= 5) || (classId >= 7 && classId <= 9) || classId == 11; }

// `*` (empty list) or a comma list of known ids. False with `error` set when a token is not one.
bool ParseIdList(std::string_view text, bool (*known)(uint32), char const* what, std::vector<uint8>& out, std::string& error)
{
    out.clear();
    if (text == "*")
        return true;
    for (std::string_view token : Split(text, ','))
    {
        uint32 id = 0;
        if (!ParseUInt(token, id) || !known(id))
        {
            error = std::string(what) + " '" + std::string(token) + "' is not '*' or a known " + what + " id";
            return false;
        }
        if (std::find(out.begin(), out.end(), uint8(id)) == out.end())
            out.push_back(uint8(id));
    }
    return !out.empty();
}

bool Contains(std::vector<uint8> const& ids, uint8 id)
{
    return ids.empty() || std::find(ids.begin(), ids.end(), id) != ids.end();
}

// Integer below 10000 for the pair chance roll.
uint32 ChanceInTenThousandths(float chance)
{
    return uint32(ClampPairChance(chance) * 10000.0f + 0.5f);
}
} // namespace

bool IsValidFirstName(std::string_view name)
{
    if (name.size() < 2 || name.size() > 12)
        return false;
    return std::all_of(name.begin(), name.end(), IsAsciiLetter);
}

bool IsValidSurname(std::string_view surname)
{
    if (surname.size() < 2 || surname.size() > 24)
        return false;
    if (!IsAsciiLetter(surname.front()) || !IsAsciiLetter(surname.back()))
        return false;
    bool previousSeparator = false;
    for (char c : surname)
    {
        bool const separator = c == ' ' || c == '\'' || c == '-';
        if (!separator && !IsAsciiLetter(c))
            return false;
        if (separator && previousSeparator)
            return false;
        previousSeparator = separator;
    }
    return true;
}

bool IsKnownStyle(std::string_view style)
{
    return style == "normal" || style == "funny" || style == "lore";
}

std::vector<Row> Parse(std::istream& in, std::vector<ParseError>& errors)
{
    std::vector<Row> rows;
    std::string line;
    uint32 lineNo = 0;
    while (std::getline(in, line))
    {
        ++lineNo;
        std::string_view const text = Trim(line);
        if (text.empty() || text.front() == '#')
            continue;

        auto skip = [&](std::string message) { errors.push_back({ lineNo, std::move(message) }); };

        std::vector<std::string_view> const fields = Split(text, '|');
        Row row;
        if (fields[0] == "first")
            row.RowKind = Kind::First;
        else if (fields[0] == "last")
            row.RowKind = Kind::Last;
        else if (fields[0] == "pair")
            row.RowKind = Kind::Pair;
        else
        {
            skip("kind '" + std::string(fields[0]) + "' is not first, last or pair");
            continue;
        }

        size_t const wanted = row.RowKind == Kind::Pair ? 8 : 7;
        if (fields.size() != wanted)
        {
            skip(std::to_string(fields.size()) + " fields, a " + std::string(fields[0]) + " row has " + std::to_string(wanted));
            continue;
        }

        if (!IsKnownStyle(fields[1]))
        {
            skip("style '" + std::string(fields[1]) + "' is not normal, funny or lore");
            continue;
        }
        row.Style = std::string(fields[1]);

        std::string error;
        if (!ParseIdList(fields[2], IsKnownRace, "race", row.Races, error) || !ParseIdList(fields[3], IsKnownClass, "class", row.Classes, error))
        {
            skip(error.empty() ? std::string("empty race or class list") : error);
            continue;
        }

        if (fields[4] != "*" && fields[4] != "m" && fields[4] != "f")
        {
            skip("sex '" + std::string(fields[4]) + "' is not *, m or f");
            continue;
        }
        row.Sex = fields[4][0];

        // an empty weight is 1 (the file header's default)
        if (fields[5].empty())
            row.Weight = 1;
        else if (!ParseUInt(fields[5], row.Weight) || row.Weight < 1 || row.Weight > 100)
        {
            skip("weight '" + std::string(fields[5]) + "' is not 1..100");
            continue;
        }

        if (row.RowKind == Kind::First || row.RowKind == Kind::Pair)
        {
            row.First = std::string(fields[6]);
            if (!IsValidFirstName(row.First))
            {
                skip("first name '" + row.First + "' is not one word of 2-12 letters");
                continue;
            }
        }
        if (row.RowKind == Kind::Last || row.RowKind == Kind::Pair)
        {
            row.Last = std::string(fields[row.RowKind == Kind::Pair ? 7 : 6]);
            if (!IsValidSurname(row.Last))
            {
                skip("surname '" + row.Last + "' is not 2-24 letters, spaces, apostrophes or hyphens");
                continue;
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<StyleWeight> ParseStyleWeights(std::string_view text, std::vector<std::string>& errors)
{
    std::vector<StyleWeight> styles;
    uint32 total = 0;
    for (std::string_view token : Split(text, ','))
    {
        if (token.empty())
            continue;
        size_t const colon = token.find(':');
        StyleWeight entry;
        std::string_view const style = colon == std::string_view::npos ? token : Trim(token.substr(0, colon));
        if (colon == std::string_view::npos || !IsKnownStyle(style) || !ParseUInt(Trim(token.substr(colon + 1)), entry.Weight))
        {
            errors.push_back("'" + std::string(token) + "' is not <normal|funny|lore>:<weight>");
            continue;
        }
        entry.Style = std::string(style);
        total += entry.Weight;
        styles.push_back(std::move(entry));
    }
    if (total == 0)
    {
        if (text != DEF_STYLE_WEIGHTS)
        {
            errors.push_back("no style with a weight above 0, the default " + std::string(DEF_STYLE_WEIGHTS) + " is used");
            std::vector<std::string> ignored;
            return ParseStyleWeights(DEF_STYLE_WEIGHTS, ignored);
        }
        return { { "normal", 1 } };
    }
    return styles;
}

float ClampPairChance(float raw)
{
    if (!(raw > 0.0f))     // also NaN
        return 0.0f;
    return std::min(raw, 1.0f);
}

bool Fits(Row const& row, BotKey const& bot)
{
    if (!Contains(row.Races, bot.Race) || !Contains(row.Classes, bot.Class))
        return false;
    return row.Sex == '*' || (row.Sex == 'm' && bot.Sex == 0) || (row.Sex == 'f' && bot.Sex == 1);
}

std::string PickStyle(std::vector<StyleWeight> const& styles, Rng const& rng)
{
    uint32 total = 0;
    for (StyleWeight const& entry : styles)
        total += entry.Weight;
    if (total == 0)
        return "normal";
    uint32 roll = rng(0, total - 1);
    for (StyleWeight const& entry : styles)
    {
        if (roll < entry.Weight)
            return entry.Style;
        roll -= entry.Weight;
    }
    return styles.back().Style;
}

Row const* PickWeighted(std::vector<Row const*> const& candidates, Rng const& rng)
{
    uint32 total = 0;
    for (Row const* row : candidates)
        total += row->Weight;
    if (total == 0)
        return nullptr;
    uint32 roll = rng(0, total - 1);
    for (Row const* row : candidates)
    {
        if (roll < row->Weight)
            return row;
        roll -= row->Weight;
    }
    return candidates.back();
}

std::vector<Row const*> Candidates(std::vector<Row> const& rows, Kind kind, std::string_view style, BotKey const& bot)
{
    std::vector<Row const*> found;
    for (Row const& row : rows)
        if (row.RowKind == kind && row.Style == style && Fits(row, bot))
            found.push_back(&row);
    if (!found.empty())
        return found;

    for (Row const& row : rows)
        if (row.RowKind == kind && row.Style == "normal" && row.Races.empty() && Fits(row, bot))
            found.push_back(&row);
    return found;
}

Choice Draw(std::vector<Row> const& rows, Settings const& settings, BotKey const& bot, Rng const& rng)
{
    Choice choice;
    choice.Style = PickStyle(settings.Styles, rng);

    // a pair only of the drawn style (no fallback: a pair is a fixed full name of that style)
    std::vector<Row const*> pairs;
    for (Row const& row : rows)
        if (row.RowKind == Kind::Pair && row.Style == choice.Style && Fits(row, bot))
            pairs.push_back(&row);
    uint32 const chance = ChanceInTenThousandths(settings.PairChance);
    if (!pairs.empty() && chance > 0 && rng(0, 9999) < chance)
    {
        Row const* pair = PickWeighted(pairs, rng);
        choice.First = pair->First;
        choice.Last = pair->Last;
        choice.FromPair = true;
        return choice;
    }

    if (Row const* first = PickWeighted(Candidates(rows, Kind::First, choice.Style, bot), rng))
        choice.First = first->First;
    if (Row const* last = PickWeighted(Candidates(rows, Kind::Last, choice.Style, bot), rng))
        choice.Last = last->Last;
    return choice;
}

std::string DrawSurname(std::vector<Row> const& rows, Settings const& settings, BotKey const& bot, Rng const& rng)
{
    std::string const style = PickStyle(settings.Styles, rng);
    Row const* last = PickWeighted(Candidates(rows, Kind::Last, style, bot), rng);
    return last ? last->Last : std::string();
}
}
