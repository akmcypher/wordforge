#include "core/formats/plain_text.hpp"
#include <memory>
#include <stdexcept>
#include <unicode/ucnv.h>
#include <unicode/ucsdet.h>

namespace wf {
namespace {
bool validUtf8(const std::string &bytes) {
    for (std::size_t i = 0; i < bytes.size();) {
        const auto c = static_cast<unsigned char>(bytes[i]);
        if (c < 0x80) {
            ++i;
            continue;
        }
        const std::size_t length = c >= 0xc2 && c <= 0xdf   ? 2
                                   : c >= 0xe0 && c <= 0xef ? 3
                                   : c >= 0xf0 && c <= 0xf4 ? 4
                                                            : 0;
        if (!length || i + length > bytes.size())
            return false;
        for (std::size_t k = 1; k < length; ++k)
            if ((static_cast<unsigned char>(bytes[i + k]) & 0xc0) != 0x80)
                return false;
        i += length;
    }
    return true;
}
std::u16string convert(const std::string &bytes, const char *encoding) {
    UErrorCode error = U_ZERO_ERROR;
    std::unique_ptr<UConverter, decltype(&ucnv_close)> converter(ucnv_open(encoding, &error),
                                                                 ucnv_close);
    if (U_FAILURE(error))
        throw std::runtime_error(std::string("Unknown text encoding: ") + encoding);
    std::u16string out(bytes.size() * 2 + 4, u'\0');
    const auto length = ucnv_toUChars(converter.get(), reinterpret_cast<UChar *>(out.data()),
                                      static_cast<int32_t>(out.size()), bytes.data(),
                                      static_cast<int32_t>(bytes.size()), &error);
    if (U_FAILURE(error))
        throw std::runtime_error(std::string("Cannot decode the text as ") + encoding);
    out.resize(static_cast<std::size_t>(length));
    return out;
}
// UTF-16 without a byte-order mark: mostly zero high bytes in one byte position.
const char *unmarkedUtf16(const std::string &bytes) {
    if (bytes.size() < 4 || bytes.size() % 2)
        return nullptr;
    std::size_t evenZero = 0, oddZero = 0;
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        evenZero += bytes[i] == '\0';
        oddZero += bytes[i + 1] == '\0';
    }
    const auto pairs = bytes.size() / 2;
    if (oddZero * 10 >= pairs * 4 && evenZero * 10 < pairs)
        return "UTF-16LE";
    if (evenZero * 10 >= pairs * 4 && oddZero * 10 < pairs)
        return "UTF-16BE";
    return nullptr;
}
// Thai in windows-874 (TIS-620 plus Windows punctuation), which ICU's detector does not know.
// Thai spelling is regular: vowel and tone marks follow a consonant (or stack on another
// mark), leading vowels precede one, and some byte values are never used. Chinese, Japanese
// and Korean double-byte text breaks these rules within a sentence.
bool looksThai(const std::string &bytes) {
    auto at = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };
    auto consonant = [](unsigned char c) { return c >= 0xa1 && c <= 0xce; };
    auto mark = [](unsigned char c) {
        return c == 0xd1 || (c >= 0xd4 && c <= 0xda) || (c >= 0xe7 && c <= 0xee);
    };
    std::size_t high = 0, consonants = 0, broken = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto c = at(i);
        if (c < 0x80)
            continue;
        ++high;
        // Big5 and Shift_JIS put ASCII letters after a lead byte; Thai letters run into
        // English words practically never.
        if (i + 1 < bytes.size() && at(i + 1) >= 0x40 && at(i + 1) <= 0x7e)
            ++broken;
        if ((c >= 0xdb && c <= 0xde) || c >= 0xfc ||
            (c >= 0x81 && c <= 0x9f && c != 0x85 && (c < 0x91 || c > 0x97)))
            return false; // unassigned in windows-874
        if (consonant(c))
            ++consonants;
        else if (mark(c) && (i == 0 || !(consonant(at(i - 1)) || mark(at(i - 1)))))
            ++broken;
        else if (c >= 0xe0 && c <= 0xe4 && (i + 1 == bytes.size() || !consonant(at(i + 1))))
            ++broken;
    }
    return high >= 6 && consonants * 4 >= high && broken * 50 <= high;
}
std::string detect(const std::string &bytes) {
    if (looksThai(bytes))
        return "windows-874";
    UErrorCode error = U_ZERO_ERROR;
    std::unique_ptr<UCharsetDetector, decltype(&ucsdet_close)> detector(ucsdet_open(&error),
                                                                        ucsdet_close);
    if (U_FAILURE(error))
        return "windows-1252";
    ucsdet_setText(detector.get(), bytes.data(), static_cast<int32_t>(bytes.size()), &error);
    const auto *match = ucsdet_detect(detector.get(), &error);
    if (U_FAILURE(error) || !match)
        return "windows-1252";
    const char *name = ucsdet_getName(match, &error);
    if (U_FAILURE(error) || !name)
        return "windows-1252";
    std::string encoding = name;
    // ISO-8859-1 is reported for Windows text too; windows-1252 is its superset.
    if (encoding == "ISO-8859-1")
        return "windows-1252";
    if (encoding == "GB2312" || encoding == "GBK")
        return "GB18030";
    return encoding;
}
} // namespace

DecodedText decodePlainText(const std::string &bytes) {
    DecodedText result;
    auto starts = [&](std::string_view mark) { return bytes.compare(0, mark.size(), mark) == 0; };
    std::string body = bytes;
    if (starts("\xef\xbb\xbf")) {
        result.encoding = "UTF-8";
        body.erase(0, 3);
    } else if (starts(std::string_view("\xff\xfe\0\0", 4))) {
        result.encoding = "UTF-32LE";
        body.erase(0, 4);
    } else if (starts(std::string_view("\0\0\xfe\xff", 4))) {
        result.encoding = "UTF-32BE";
        body.erase(0, 4);
    } else if (starts("\xff\xfe")) {
        result.encoding = "UTF-16LE";
        body.erase(0, 2);
    } else if (starts("\xfe\xff")) {
        result.encoding = "UTF-16BE";
        body.erase(0, 2);
    } else if (const auto *utf16 = unmarkedUtf16(bytes)) // before UTF-8: NULs are valid UTF-8
        result.encoding = utf16;
    else if (validUtf8(bytes))
        result.encoding = "UTF-8";
    else
        result.encoding = detect(bytes);
    const auto decoded = convert(body, result.encoding.c_str());
    result.text.reserve(decoded.size());
    for (std::size_t i = 0; i < decoded.size(); ++i) {
        const char16_t c = decoded[i];
        if (c == u'\r') {
            result.text += u'\n';
            if (i + 1 < decoded.size() && decoded[i + 1] == u'\n')
                ++i;
        } else if (c == u'\n' || c == u'\f' || c == u'\v' || c == u' ' || c == u' ' ||
                   c == u'\u0085')
            result.text += u'\n';
        else if (c == u'\t' || (c >= 0x20 && c != 0x7f && c != 0xfeff && (c < 0x80 || c > 0x9f)))
            result.text += c;
    }
    return result;
}
} // namespace wf
