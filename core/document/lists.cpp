#include "core/document/lists.hpp"
#include "core/document/document.hpp"
#include <algorithm>
#include <stdexcept>

namespace wf {
void validateListDefinition(const ListDefinition &definition) {
    if (!definition.id)
        throw std::invalid_argument("Invalid list identifier");
    for (const auto &level : definition.levels)
        if (static_cast<unsigned>(level.format) > static_cast<unsigned>(NumberFormat::None) ||
            level.text.size() > 32 || level.start > 32767 ||
            std::any_of(level.text.begin(), level.text.end(),
                        [](char16_t ch) { return ch < 32; }))
            throw std::invalid_argument("Invalid list level");
}

ListLevel builtinListLevel(ListKind kind, std::size_t level) {
    const auto number = std::to_string(level + 1);
    if (kind == ListKind::Bullet) {
        static const char16_t *bullets[] = {u"•", u"◦", u"▪"};
        return {NumberFormat::Bullet, bullets[level % 3], 1};
    }
    static const NumberFormat formats[] = {NumberFormat::Decimal, NumberFormat::LowerLetter,
                                           NumberFormat::LowerRoman};
    return {formats[level % 3], u"%" + std::u16string(number.begin(), number.end()) + u".", 1};
}

std::u16string formatListNumber(std::uint32_t number, NumberFormat format) {
    auto ascii = [](const std::string &text) { return std::u16string(text.begin(), text.end()); };
    switch (format) {
    case NumberFormat::LowerLetter:
    case NumberFormat::UpperLetter: {
        if (number == 0)
            return u"0";
        // Word repeats the letter: 27 is "aa", 53 is "aaa".
        const char letter = static_cast<char>(
            (format == NumberFormat::LowerLetter ? 'a' : 'A') + (number - 1) % 26);
        return ascii(std::string((number - 1) / 26 + 1, letter));
    }
    case NumberFormat::LowerRoman:
    case NumberFormat::UpperRoman: {
        if (number == 0)
            return u"0";
        static const std::pair<std::uint32_t, const char *> parts[] = {
            {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"}, {50, "l"},
            {40, "xl"},  {10, "x"},   {9, "ix"},  {5, "v"},    {4, "iv"},  {1, "i"}};
        std::string text;
        for (const auto &[value, letters] : parts)
            while (number >= value) {
                text += letters;
                number -= value;
            }
        if (format == NumberFormat::UpperRoman)
            std::transform(text.begin(), text.end(), text.begin(),
                           [](char ch) { return static_cast<char>(ch - 'a' + 'A'); });
        return ascii(text);
    }
    case NumberFormat::Bullet:
    case NumberFormat::None:
        return {};
    case NumberFormat::Decimal:
        break;
    }
    return ascii(std::to_string(number));
}

ListNumbering::ListNumbering(const ListDefinitions &definitions) : definitions_(definitions) {}

std::u16string ListNumbering::next(ListKind kind, std::uint64_t list, std::size_t level,
                                   std::uint64_t container) {
    // Wordforge's own lists restart outside a list and in each table cell.
    auto restartOwn = [&] {
        if (!ownActive_)
            return;
        counts_.erase({0, static_cast<int>(ListKind::Bullet)});
        counts_.erase({0, static_cast<int>(ListKind::Decimal)});
        ownActive_ = false;
    };
    if (started_ && container != container_)
        restartOwn();
    started_ = true;
    container_ = container;
    if (kind == ListKind::None) {
        restartOwn();
        return {};
    }
    level = std::min(level, listLevels - 1);
    const ListDefinition *definition = nullptr;
    if (list)
        for (const auto &candidate : definitions_)
            if (candidate.id == list)
                definition = &candidate;
    auto levelAt = [&](std::size_t n) {
        return definition ? definition->levels[n] : builtinListLevel(kind, n);
    };
    // A list seen for the first time (or restarted) has no number yet (-1) at any level.
    const auto [entry, fresh] =
        counts_.try_emplace({definition ? list : 0, definition ? 0 : static_cast<int>(kind)});
    auto &counts = entry->second;
    if (!definition)
        ownActive_ = true;
    if (fresh)
        counts.fill(-1);
    const auto current = levelAt(level);
    counts[level] = counts[level] < 0 ? current.start : counts[level] + 1;
    for (auto deeper = level + 1; deeper < listLevels; ++deeper)
        counts[deeper] = -1;
    if (current.format == NumberFormat::Bullet)
        return current.text;
    std::u16string label;
    for (std::size_t i = 0; i < current.text.size(); ++i) {
        const auto ch = current.text[i];
        if (ch == u'%' && i + 1 < current.text.size() && current.text[i + 1] >= u'1' &&
            current.text[i + 1] <= u'9') {
            const auto referenced = static_cast<std::size_t>(current.text[i + 1] - u'1');
            const auto source = levelAt(referenced);
            const auto value = counts[referenced] < 0 ? source.start : counts[referenced];
            label += formatListNumber(static_cast<std::uint32_t>(value),
                                      source.format == NumberFormat::Bullet ||
                                              source.format == NumberFormat::None
                                          ? NumberFormat::Decimal
                                          : source.format);
            ++i;
        } else
            label += ch;
    }
    return label;
}
} // namespace wf
