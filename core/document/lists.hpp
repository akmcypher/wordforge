#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace wf {
enum class ListKind;

// List numbering, as Word's numbering definitions describe it: up to nine levels, each
// with its own number format, label and starting number.
constexpr std::size_t listLevels = 9;
enum class NumberFormat : std::uint8_t {
    Decimal,
    LowerLetter, // a, b ... z, aa, bb
    UpperLetter,
    LowerRoman, // i, ii, iii
    UpperRoman,
    Bullet, // the label is the bullet character itself
    None,   // the label is shown as written, without a number
};
struct ListLevel {
    NumberFormat format = NumberFormat::Decimal;
    // The label: "%1" ... "%9" stand for the numbers of levels 1-9 ("%1.%2." gives
    // "1.3."); for a bullet, the bullet character. At most 32 characters.
    std::u16string text;
    std::uint32_t start = 1; // at most 32767
    bool operator==(const ListLevel &) const = default;
};
// A list a document's paragraphs can share (a Word numbering instance): its paragraphs
// are numbered together, wherever they are in the document.
struct ListDefinition {
    std::uint64_t id = 0;
    std::array<ListLevel, listLevels> levels;
    bool operator==(const ListDefinition &) const = default;
};
using ListDefinitions = std::vector<ListDefinition>;
void validateListDefinition(const ListDefinition &);
// Wordforge's own lists (a paragraph's list kind without a definition): bullets • ◦ ▪,
// and numbers 1. a. i. repeating by level.
ListLevel builtinListLevel(ListKind, std::size_t level);
std::u16string formatListNumber(std::uint32_t number, NumberFormat);

// Numbers list paragraphs in document order. Paragraphs of one definition are numbered
// together through the whole document; Wordforge's own lists restart after a paragraph
// that isn't in a list and in each new table cell. A level's number restarts after a
// paragraph at a shallower level of the same list.
class ListNumbering {
  public:
    explicit ListNumbering(const ListDefinitions &);
    // The label of the next paragraph: its list kind (None for a paragraph outside a
    // list), definition (0 for Wordforge's own), level and container (any value that
    // differs between table cells and the body). Empty outside a list.
    std::u16string next(ListKind kind, std::uint64_t list, std::size_t level,
                        std::uint64_t container);

  private:
    const ListDefinitions &definitions_;
    std::map<std::pair<std::uint64_t, int>, std::array<std::int64_t, listLevels>> counts_;
    std::uint64_t container_ = 0;
    bool started_ = false, ownActive_ = false; // ownActive_: an own list has counts
};
} // namespace wf
