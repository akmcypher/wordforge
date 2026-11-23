#include "core/document/document.hpp"
#include <memory>
#include <stdexcept>
#include <unicode/ubrk.h>
#include <unicode/uchar.h>
#include <unicode/utf16.h>
namespace wf {
namespace {
using Breaks = std::unique_ptr<UBreakIterator, decltype(&ubrk_close)>;
Breaks breaks(UBreakIteratorType type) {
    UErrorCode error = U_ZERO_ERROR;
    Breaks iterator(ubrk_open(type, "und", nullptr, 0, &error), ubrk_close);
    if (U_FAILURE(error))
        throw std::runtime_error("Cannot open a text break iterator");
    return iterator;
}
// Counts `text` with two reusable iterators (opening them dominates short texts).
TextCount countWith(const std::u16string &text, UBreakIterator *words,
                    UBreakIterator *characters) {
    TextCount count;
    const auto *data = reinterpret_cast<const UChar *>(text.data());
    const auto length = static_cast<int32_t>(text.size());
    UErrorCode error = U_ZERO_ERROR;
    ubrk_setText(words, data, length, &error);
    ubrk_setText(characters, data, length, &error);
    if (U_FAILURE(error))
        throw std::runtime_error("Cannot count text");
    for (auto at = ubrk_first(words); at != UBRK_DONE; at = ubrk_next(words))
        if (ubrk_getRuleStatus(words) >= UBRK_WORD_NUMBER)
            ++count.words;
    bool lineHasText = false;
    for (auto boundary = ubrk_first(characters); boundary != UBRK_DONE && boundary < length;
         boundary = ubrk_next(characters)) {
        UChar32 first;
        U16_GET(data, 0, boundary, length, first);
        if (first == u'\n') {
            lineHasText = false;
            continue;
        }
        ++count.characters;
        if (!u_isUWhiteSpace(first)) {
            ++count.charactersWithoutSpaces;
            if (!lineHasText)
                ++count.paragraphs;
            lineHasText = true;
        }
    }
    return count;
}
} // namespace
TextCount countText(const std::u16string &text) {
    auto words = breaks(UBRK_WORD), characters = breaks(UBRK_CHARACTER);
    return countWith(text, words.get(), characters.get());
}
namespace {
void add(TextCount &total, const TextCount &part, bool subtract = false) {
    auto apply = [subtract](std::size_t &to, std::size_t value) {
        to = subtract ? to - value : to + value;
    };
    apply(total.words, part.words);
    apply(total.characters, part.characters);
    apply(total.charactersWithoutSpaces, part.charactersWithoutSpaces);
    apply(total.paragraphs, part.paragraphs);
}
} // namespace
const TextCount &DocumentCounter::count(const Document &document) {
    const auto &now = document.paragraphs;
    std::size_t prefix = 0, suffix = 0;
    while (prefix < previous_.size() && prefix < now.size() && now.shares(prefix, previous_, prefix))
        ++prefix;
    while (suffix < previous_.size() - prefix && suffix < now.size() - prefix &&
           now.shares(now.size() - 1 - suffix, previous_, previous_.size() - 1 - suffix))
        ++suffix;
    const auto oldEnd = previous_.size() - suffix, newEnd = now.size() - suffix;
    for (auto i = prefix; i < oldEnd; ++i)
        add(total_, counts_[i], true);
    std::vector<TextCount> changed;
    changed.reserve(newEnd - prefix);
    Breaks words(nullptr, ubrk_close), characters(nullptr, ubrk_close);
    if (prefix < newEnd) {
        words = breaks(UBRK_WORD);
        characters = breaks(UBRK_CHARACTER);
    }
    for (auto i = prefix; i < newEnd; ++i) {
        changed.push_back(countWith(now[i].text(), words.get(), characters.get()));
        add(total_, changed.back());
    }
    const auto at = counts_.begin() + static_cast<std::ptrdiff_t>(prefix);
    counts_.erase(at, counts_.begin() + static_cast<std::ptrdiff_t>(oldEnd));
    counts_.insert(counts_.begin() + static_cast<std::ptrdiff_t>(prefix), changed.begin(),
                   changed.end());
    previous_ = now;
    return total_;
}
} // namespace wf
