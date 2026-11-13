#include "core/document/document.hpp"
#include <memory>
#include <unicode/ubrk.h>
#include <unicode/uchar.h>
#include <unicode/utf16.h>
namespace wf {
TextCount countText(const std::u16string &text) {
    TextCount count;
    const auto *data = reinterpret_cast<const UChar *>(text.data());
    const auto length = static_cast<int32_t>(text.size());
    UErrorCode error = U_ZERO_ERROR;
    std::unique_ptr<UBreakIterator, decltype(&ubrk_close)> words(
        ubrk_open(UBRK_WORD, "und", data, length, &error), ubrk_close);
    if (U_SUCCESS(error))
        for (auto at = ubrk_first(words.get()); at != UBRK_DONE; at = ubrk_next(words.get()))
            if (ubrk_getRuleStatus(words.get()) >= UBRK_WORD_NUMBER)
                ++count.words;
    bool lineHasText = false;
    for (const auto boundary : graphemeBoundaries(text)) {
        if (boundary >= text.size())
            break;
        UChar32 first;
        U16_GET(data, 0, static_cast<int32_t>(boundary), length, first);
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
} // namespace wf
