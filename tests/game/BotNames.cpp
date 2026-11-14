#include "tc_catch2.h"
#include "BotNames.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace BotNames;

namespace
{
std::vector<Row> ParseText(std::string const& text, std::vector<ParseError>& errors)
{
    std::istringstream in(text);
    return Parse(in, errors);
}

std::vector<Row> ParseText(std::string const& text)
{
    std::vector<ParseError> errors;
    return ParseText(text, errors);
}

// Returns the given numbers in turn (each clamped into the asked range), then the lowest value.
struct ScriptedRng
{
    std::vector<uint32> Values;
    size_t Next = 0;
    uint32 operator()(uint32 lo, uint32 hi)
    {
        uint32 const value = Next < Values.size() ? Values[Next++] : lo;
        return std::min(hi, std::max(lo, value));
    }
};

// A small deterministic generator for the distribution checks (no global state, so the test gives the same result each run).
struct Lcg
{
    uint64 State = 12345;
    uint32 operator()(uint32 lo, uint32 hi)
    {
        State = State * 6364136223846793005ULL + 1442695040888963407ULL;
        return lo + uint32((State >> 33) % (uint64(hi) - lo + 1));
    }
};

Settings OnlyStyle(char const* style, float pairChance = 0.0f)
{
    Settings settings;
    settings.Styles = { { style, 1 } };
    settings.PairChance = pairChance;
    return settings;
}

BotKey const HumanRogueMale{ 1, 4, 0 };
BotKey const HumanRogueFemale{ 1, 4, 1 };
BotKey const OrcWarriorMale{ 2, 1, 0 };
}

TEST_CASE("First names follow the player-name rule", "[BotNames]")
{
    CHECK(IsValidFirstName("Al"));
    CHECK(IsValidFirstName("Aldric"));
    CHECK(IsValidFirstName("Abcdefghijkl"));     // 12
    CHECK_FALSE(IsValidFirstName("A"));
    CHECK_FALSE(IsValidFirstName("Abcdefghijklm")); // 13
    CHECK_FALSE(IsValidFirstName("Al dric"));
    CHECK_FALSE(IsValidFirstName("Al-dric"));
    CHECK_FALSE(IsValidFirstName("Ald'ric"));
    CHECK_FALSE(IsValidFirstName("Aldric1"));
    CHECK_FALSE(IsValidFirstName(""));
}

TEST_CASE("Surnames allow letters, space, apostrophe and hyphen, 2 to 24 characters", "[BotNames]")
{
    CHECK(IsValidSurname("Ox"));
    CHECK(IsValidSurname("Cult-of-the-damned"));
    CHECK(IsValidSurname("Vol'jin"));
    CHECK(IsValidSurname("Of the Wilds"));
    CHECK(IsValidSurname("Abcdefghijklmnopqrstuvwx"));   // 24
    CHECK_FALSE(IsValidSurname("Abcdefghijklmnopqrstuvwxy")); // 25
    CHECK_FALSE(IsValidSurname("O"));
    CHECK_FALSE(IsValidSurname("-Ash"));
    CHECK_FALSE(IsValidSurname("Ash-"));
    CHECK_FALSE(IsValidSurname("Ash--wood"));
    CHECK_FALSE(IsValidSurname("Ash  wood"));
    CHECK_FALSE(IsValidSurname("Ash4"));
    CHECK_FALSE(IsValidSurname("Ash|wood"));
}

TEST_CASE("Good rows of each kind are read with all their fields", "[BotNames]")
{
    std::vector<ParseError> errors;
    std::vector<Row> const rows = ParseText(
        "# a comment\n"
        "\n"
        "first|normal|1|*|m|3|Aldric\n"
        "last|funny|*|4,3|*|10|Stabsworth\r\n"
        "pair|lore|1|*|f|1|Jaina|Proudmoore\n"
        "last|normal|*|*|*||Hale\n", errors);
    CHECK(errors.empty());
    REQUIRE(rows.size() == 4);

    CHECK(rows[0].RowKind == Kind::First);
    CHECK(rows[0].Style == "normal");
    CHECK(rows[0].Races == std::vector<uint8>{ 1 });
    CHECK(rows[0].Classes.empty());
    CHECK(rows[0].Sex == 'm');
    CHECK(rows[0].Weight == 3);
    CHECK(rows[0].First == "Aldric");

    CHECK(rows[1].RowKind == Kind::Last);
    CHECK(rows[1].Races.empty());
    CHECK((rows[1].Classes == std::vector<uint8>{ 4, 3 }));
    CHECK(rows[1].Last == "Stabsworth");    // the \r of a Windows line end is not part of the name

    CHECK(rows[2].RowKind == Kind::Pair);
    CHECK(rows[2].First == "Jaina");
    CHECK(rows[2].Last == "Proudmoore");

    CHECK(rows[3].Weight == 1);             // an empty weight is 1
}

TEST_CASE("Rows that break a rule are skipped and reported with their line", "[BotNames]")
{
    std::vector<ParseError> errors;
    std::vector<Row> const rows = ParseText(
        "middle|normal|*|*|*|1|Bob\n"          // 1 kind
        "first|weird|*|*|*|1|Bob\n"            // 2 style
        "first|normal|9|*|*|1|Bob\n"           // 3 race 9 is not a race of the doc
        "first|normal|*|6|*|1|Bob\n"           // 4 class 6 (death knight) is not a class of the doc
        "first|normal|*|*|x|1|Bob\n"           // 5 sex
        "first|normal|*|*|*|0|Bob\n"           // 6 weight 0
        "first|normal|*|*|*|101|Bob\n"         // 7 weight 101
        "first|normal|*|*|*|1|Bob Jr\n"        // 8 first name with a space
        "last|normal|*|*|*|1|X\n"              // 9 surname too short
        "pair|lore|*|*|*|1|Jaina\n"            // 10 pair without a surname
        "first|normal|*|*|*|1|Bob|Extra\n"     // 11 one field too many
        "first|normal|1,x|*|*|1|Bob\n"         // 12 bad id in a list
        "first|normal|*|*|*|1|Good\n", errors); // 13 the only good row
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].First == "Good");
    REQUIRE(errors.size() == 12);
    for (uint32 i = 0; i < 12; ++i)
    {
        INFO("error " << i << ": " << errors[i].Message);
        CHECK(errors[i].Line == i + 1);
        CHECK_FALSE(errors[i].Message.empty());
    }
}

TEST_CASE("Style weights are read from the config text", "[BotNames]")
{
    std::vector<std::string> errors;
    std::vector<StyleWeight> const styles = ParseStyleWeights(DEF_STYLE_WEIGHTS, errors);
    CHECK(errors.empty());
    REQUIRE(styles.size() == 3);
    CHECK(styles[0].Style == "normal");
    CHECK(styles[0].Weight == 60);
    CHECK(styles[1].Style == "funny");
    CHECK(styles[1].Weight == 25);
    CHECK(styles[2].Style == "lore");
    CHECK(styles[2].Weight == 15);

    errors.clear();
    std::vector<StyleWeight> const partial = ParseStyleWeights(" normal : 10 , silly:5, lore:x ", errors);
    CHECK(errors.size() == 2);
    REQUIRE(partial.size() == 1);
    CHECK(partial[0].Style == "normal");
    CHECK(partial[0].Weight == 10);

    // nothing usable: the default
    errors.clear();
    std::vector<StyleWeight> const fallback = ParseStyleWeights("normal:0,funny:0", errors);
    CHECK_FALSE(errors.empty());
    CHECK(fallback.size() == 3);
}

TEST_CASE("The pair chance is clamped to 0..1", "[BotNames]")
{
    CHECK(ClampPairChance(-1.0f) == 0.0f);
    CHECK(ClampPairChance(0.05f) == 0.05f);
    CHECK(ClampPairChance(2.0f) == 1.0f);
}

TEST_CASE("A row fits by race, class and sex", "[BotNames]")
{
    std::vector<Row> const rows = ParseText(
        "first|normal|1,3|*|m|1|Aldric\n"
        "last|funny|*|4|*|1|Stabsworth\n"
        "first|normal|*|*|f|1|Anna\n");
    REQUIRE(rows.size() == 3);
    CHECK(Fits(rows[0], HumanRogueMale));
    CHECK(Fits(rows[0], BotKey{ 3, 1, 0 }));
    CHECK_FALSE(Fits(rows[0], HumanRogueFemale));
    CHECK_FALSE(Fits(rows[0], OrcWarriorMale));
    CHECK(Fits(rows[1], HumanRogueMale));
    CHECK_FALSE(Fits(rows[1], OrcWarriorMale));
    CHECK(Fits(rows[2], HumanRogueFemale));
    CHECK_FALSE(Fits(rows[2], HumanRogueMale));
}

TEST_CASE("A style is drawn by its weight", "[BotNames]")
{
    std::vector<StyleWeight> const styles = { { "normal", 60 }, { "funny", 25 }, { "lore", 15 } };
    struct Case { uint32 roll; char const* style; };
    Case const cases[] = { { 0, "normal" }, { 59, "normal" }, { 60, "funny" }, { 84, "funny" }, { 85, "lore" }, { 99, "lore" } };
    for (Case const& c : cases)
    {
        INFO("roll " << c.roll);
        ScriptedRng rng{ { c.roll } };
        CHECK(PickStyle(styles, std::ref(rng)) == c.style);
    }
    ScriptedRng rng;
    CHECK(PickStyle({}, std::ref(rng)) == "normal");
    CHECK(PickStyle({ { "lore", 0 } }, std::ref(rng)) == "normal");
}

TEST_CASE("Rows are drawn by their weight", "[BotNames]")
{
    std::vector<Row> const rows = ParseText(
        "last|normal|*|*|*|1|Rare\n"
        "last|normal|*|*|*|9|Common\n");
    std::vector<Row const*> const candidates = Candidates(rows, Kind::Last, "normal", HumanRogueMale);
    REQUIRE(candidates.size() == 2);
    ScriptedRng rng{ { 0, 1, 9 } };
    CHECK(PickWeighted(candidates, std::ref(rng))->Last == "Rare");
    CHECK(PickWeighted(candidates, std::ref(rng))->Last == "Common");
    CHECK(PickWeighted(candidates, std::ref(rng))->Last == "Common");
    CHECK(!PickWeighted({}, std::ref(rng)));

    Lcg lcg;
    std::map<std::string, uint32> counts;
    for (uint32 i = 0; i < 10000; ++i)
        ++counts[PickWeighted(candidates, std::ref(lcg))->Last];
    CHECK(counts["Rare"] > 700);
    CHECK(counts["Rare"] < 1300);
}

TEST_CASE("A style with no fitting row falls back to the normal rows for every race", "[BotNames]")
{
    std::vector<Row> const rows = ParseText(
        "last|funny|*|4|*|1|Stabsworth\n"
        "last|normal|2|*|*|1|Orcish\n"
        "last|normal|*|*|*|1|Everyman\n"
        "first|normal|1|*|*|1|Humanly\n");

    // a rogue gets the funny rogue surname
    std::vector<Row const*> rogue = Candidates(rows, Kind::Last, "funny", HumanRogueMale);
    REQUIRE(rogue.size() == 1);
    CHECK(rogue[0]->Last == "Stabsworth");

    // a warrior has no funny row: the normal `*` rows, not the orc's normal row
    std::vector<Row const*> warrior = Candidates(rows, Kind::Last, "funny", OrcWarriorMale);
    REQUIRE(warrior.size() == 1);
    CHECK(warrior[0]->Last == "Everyman");

    // the fallback does not take rows with a race list: an orc gets no first name row (the generated name)
    CHECK(Candidates(rows, Kind::First, "funny", OrcWarriorMale).empty());
    CHECK(Candidates(rows, Kind::First, "normal", OrcWarriorMale).empty());
    CHECK(Candidates(rows, Kind::First, "normal", HumanRogueMale).size() == 1);
}

TEST_CASE("A draw gives a first name and a surname of the style", "[BotNames]")
{
    std::vector<Row> const rows = ParseText(
        "first|normal|1|*|m|1|Aldric\n"
        "first|normal|1|*|f|1|Brenna\n"
        "last|normal|1|*|*|1|Ashford\n"
        "last|lore|1|*|*|1|Lordaeron\n");

    Lcg lcg;
    Choice const male = Draw(rows, OnlyStyle("normal"), HumanRogueMale, std::ref(lcg));
    CHECK(male.Style == "normal");
    CHECK(male.First == "Aldric");
    CHECK(male.Last == "Ashford");
    CHECK_FALSE(male.FromPair);

    Choice const female = Draw(rows, OnlyStyle("normal"), HumanRogueFemale, std::ref(lcg));
    CHECK(female.First == "Brenna");

    Choice const lore = Draw(rows, OnlyStyle("lore"), HumanRogueMale, std::ref(lcg));
    CHECK(lore.First.empty());          // no lore first row and no normal `*` first row: the generated name
    CHECK(lore.Last == "Lordaeron");
}

TEST_CASE("Nothing fits: a generated first name and no surname", "[BotNames]")
{
    std::vector<Row> const rows = ParseText(
        "first|normal|1|*|*|1|Aldric\n"
        "last|normal|1|*|*|1|Ashford\n");
    Lcg lcg;
    Choice const choice = Draw(rows, OnlyStyle("normal"), OrcWarriorMale, std::ref(lcg));
    CHECK(choice.First.empty());
    CHECK(choice.Last.empty());
    CHECK(DrawSurname(rows, OnlyStyle("normal"), OrcWarriorMale, std::ref(lcg)).empty());

    Choice const empty = Draw({}, OnlyStyle("normal"), HumanRogueMale, std::ref(lcg));
    CHECK(empty.First.empty());
    CHECK(empty.Last.empty());
}

TEST_CASE("A pair is used only with the pair chance and only of the drawn style", "[BotNames]")
{
    std::vector<Row> const rows = ParseText(
        "pair|lore|1|*|*|1|Jaina|Proudmoore\n"
        "last|lore|1|*|*|1|Lordaeron\n");

    // style roll 0, pair roll 499 < 500 (5 %): the pair
    ScriptedRng hit{ { 0, 499, 0 } };
    Choice const pair = Draw(rows, OnlyStyle("lore", 0.05f), HumanRogueMale, std::ref(hit));
    CHECK(pair.FromPair);
    CHECK(pair.First == "Jaina");
    CHECK(pair.Last == "Proudmoore");

    // pair roll 500: no pair
    ScriptedRng miss{ { 0, 500, 0, 0 } };
    Choice const single = Draw(rows, OnlyStyle("lore", 0.05f), HumanRogueMale, std::ref(miss));
    CHECK_FALSE(single.FromPair);
    CHECK(single.Last == "Lordaeron");

    // chance 0: never; another race: never; another style: never
    Lcg lcg;
    for (uint32 i = 0; i < 200; ++i)
    {
        CHECK_FALSE(Draw(rows, OnlyStyle("lore", 0.0f), HumanRogueMale, std::ref(lcg)).FromPair);
        CHECK_FALSE(Draw(rows, OnlyStyle("lore", 1.0f), OrcWarriorMale, std::ref(lcg)).FromPair);
        CHECK_FALSE(Draw(rows, OnlyStyle("normal", 1.0f), HumanRogueMale, std::ref(lcg)).FromPair);
    }
    // chance 1: always
    CHECK(Draw(rows, OnlyStyle("lore", 1.0f), HumanRogueMale, std::ref(lcg)).FromPair);
}

TEST_CASE("A backfill draws only a surname", "[BotNames]")
{
    std::vector<Row> const rows = ParseText(
        "pair|normal|*|*|*|100|Jaina|Proudmoore\n"
        "last|normal|*|*|*|1|Everyman\n");
    Lcg lcg;
    for (uint32 i = 0; i < 50; ++i)
        CHECK(DrawSurname(rows, OnlyStyle("normal", 1.0f), HumanRogueMale, std::ref(lcg)) == "Everyman");
}

TEST_CASE("The styles follow their weights over many draws", "[BotNames]")
{
    std::vector<Row> const rows = ParseText(
        "last|normal|*|*|*|1|Plain\n"
        "last|funny|*|*|*|1|Silly\n"
        "last|lore|*|*|*|1|Old\n");
    Settings settings;
    std::vector<std::string> errors;
    settings.Styles = ParseStyleWeights(DEF_STYLE_WEIGHTS, errors);
    settings.PairChance = DEF_PAIR_CHANCE;
    Lcg lcg;
    std::map<std::string, uint32> counts;
    for (uint32 i = 0; i < 10000; ++i)
        ++counts[Draw(rows, settings, OrcWarriorMale, std::ref(lcg)).Last];
    CHECK(counts["Plain"] > 5500);
    CHECK(counts["Plain"] < 6500);
    CHECK(counts["Silly"] > 2000);
    CHECK(counts["Silly"] < 3000);
    CHECK(counts["Old"] > 1100);
    CHECK(counts["Old"] < 1900);
}
