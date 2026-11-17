#pragma once
#include <string>

namespace wf {
struct DecodedText {
    std::u16string text;  // paragraphs separated by '\n'; other control characters removed
    std::string encoding; // what was used, e.g. "UTF-8", "UTF-16LE", "Shift_JIS"
};
// Decodes a plain-text file from any common encoding: a byte-order mark (UTF-8, UTF-16,
// UTF-32) wins; then UTF-16 without a mark; then valid UTF-8; then ICU's charset detection
// (Shift_JIS, GB18030, Big5, EUC-KR, ISO-8859-x, windows-125x, KOI8-R...), falling back to
// windows-1252. Line endings (CRLF, CR, U+2028/9, form feeds) become paragraph breaks.
DecodedText decodePlainText(const std::string &bytes);
} // namespace wf
