/*
 * Bot names: a first name and a surname for a new bot, from the names file (Bot.Names.File, format in the bots module's
 * doc/bot-names.md). Pure: the parser and the picker work on strings and numbers only (no player, world or database objects,
 * no logging, the random numbers come from the caller), so they can be tested alone. BotMgr loads the file once at start
 * (BotMgr::LoadRegistry), logs the rows the parser skipped, and reads the table afterwards without changing it.
 */

#ifndef TRINITY_BOT_NAMES_H
#define TRINITY_BOT_NAMES_H

#include "Define.h"
#include <functional>
#include <istream>
#include <string>
#include <string_view>
#include <vector>

namespace BotNames
{
enum class Kind : uint8
{
    First,  // a first name
    Last,   // a surname
    Pair    // a first name and a surname that are always used together
};

// One row of the names file.
struct Row
{
    Kind RowKind = Kind::First;
    std::string Style;              // normal, funny or lore
    std::vector<uint8> Races;       // empty = every race (`*`)
    std::vector<uint8> Classes;     // empty = every class (`*`)
    char Sex = '*';                 // '*', 'm' or 'f'
    uint32 Weight = 1;              // 1..100
    std::string First;              // First and Pair rows
    std::string Last;               // Last and Pair rows
};

// A row the parser skipped: its line number (from 1) and why.
struct ParseError
{
    uint32 Line = 0;
    std::string Message;
};

struct StyleWeight
{
    std::string Style;
    uint32 Weight = 0;
};

inline constexpr char const* DEF_STYLE_WEIGHTS = "normal:60,funny:25,lore:15";
inline constexpr float DEF_PAIR_CHANCE = 0.05f;
// Draws of a name + surname before the surname is dropped (the bot gets a generated first name alone, as before the names file).
inline constexpr uint32 MAX_DRAWS = 200;

struct Settings
{
    std::vector<StyleWeight> Styles;    // Bot.Names.StyleWeights
    float PairChance = DEF_PAIR_CHANCE; // Bot.Names.PairChance, 0..1
};

// What the picker needs to know of the bot. Sex: 0 male, 1 female (the core's Gender).
struct BotKey
{
    uint8 Race = 0;
    uint8 Class = 0;
    uint8 Sex = 0;
};

// The picker's random numbers: a uniform integer in [lo, hi] (both included), as urand gives.
using Rng = std::function<uint32(uint32 lo, uint32 hi)>;

// One draw. An empty First means "no first-name row fits: use the generated syllable name"; an empty Last means no surname.
struct Choice
{
    std::string Style;
    std::string First;
    std::string Last;
    bool FromPair = false;
};

// The player-name rule for a first name: one word, ASCII letters only, 2 to 12 characters.
bool IsValidFirstName(std::string_view name);
// A surname: ASCII letters, space, apostrophe and hyphen, 2 to 24 characters, starting and ending with a letter, no two
// separators in a row.
bool IsValidSurname(std::string_view surname);
bool IsKnownStyle(std::string_view style);

// Reads the rows of a names file. A row that breaks a rule is left out and reported in `errors`; comments (`#`) and blank lines
// are ignored.
std::vector<Row> Parse(std::istream& in, std::vector<ParseError>& errors);

// "normal:60,funny:25,lore:15". Bad entries are reported in `errors` and left out; when no entry with a weight above 0 is left the
// default (DEF_STYLE_WEIGHTS) is returned.
std::vector<StyleWeight> ParseStyleWeights(std::string_view text, std::vector<std::string>& errors);
// Bot.Names.PairChance clamped to 0..1.
float ClampPairChance(float raw);

// Whether a row may be used for this bot (race and class lists, sex).
bool Fits(Row const& row, BotKey const& bot);

// A style by its weight ("normal" when no weight is above 0).
std::string PickStyle(std::vector<StyleWeight> const& styles, Rng const& rng);

// One weighted row among `candidates` (nullptr when it is empty).
Row const* PickWeighted(std::vector<Row const*> const& candidates, Rng const& rng);

// The rows of `kind` and `style` that fit the bot; when none does, the rows of `kind`, style normal and races `*` that fit it.
std::vector<Row const*> Candidates(std::vector<Row> const& rows, Kind kind, std::string_view style, BotKey const& bot);

// One draw of a full name for a new bot: a style, then a pair row (with probability PairChance, when one of the style fits), else a
// first-name row and a surname row of the style (each with the normal `*` fallback; an empty field when nothing fits).
Choice Draw(std::vector<Row> const& rows, Settings const& settings, BotKey const& bot, Rng const& rng);

// One draw of a surname alone, for a bot that already has its first name (Bot.Names.Backfill). Empty when nothing fits.
std::string DrawSurname(std::vector<Row> const& rows, Settings const& settings, BotKey const& bot, Rng const& rng);
}

#endif // TRINITY_BOT_NAMES_H
