#include "apps/desktop/spelling.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QStandardPaths>
#include <algorithm>
#include <hunspell.hxx>
#include <unicode/ubrk.h>
#include <unicode/uchar.h>
#include <unicode/uscript.h>
#include <unicode/utf16.h>
namespace wf {
SpellChecker::SpellChecker() = default;
SpellChecker::~SpellChecker() = default;

SpellChecker &SpellChecker::shared() {
    static SpellChecker checker;
    if (!checker.loadAttempted_) {
        checker.loadAttempted_ = true;
        checker.setPersonalDictionary(
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
            "/personal-dictionary.txt");
        checker.load(QLocale::system().name());
    }
    return checker;
}

QStringList SpellChecker::searchPaths() {
    QStringList paths;
    if (const auto custom = qEnvironmentVariable("WORDFORGE_DICTIONARIES"); !custom.isEmpty())
        paths += custom.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    const auto app = QCoreApplication::applicationDirPath();
    paths << app + "/dictionaries" << app + "/../share/hunspell";
#ifdef _WIN32
    // MSYS2 installs dictionaries beside its bin folder, which is on PATH for builds.
    for (const auto &entry : qEnvironmentVariable("PATH").split(QDir::listSeparator(),
                                                                Qt::SkipEmptyParts))
        if (QFileInfo::exists(entry + "/../share/hunspell"))
            paths << entry + "/../share/hunspell";
#else
    if (const auto snap = qEnvironmentVariable("SNAP"); !snap.isEmpty())
        paths << snap + "/usr/share/hunspell"; // the host's folder is not readable
    paths << QDir::homePath() + "/.local/share/hunspell" << "/usr/share/hunspell"
          << "/usr/share/myspell" << "/usr/share/myspell/dicts";
#endif
    QStringList existing;
    for (const auto &path : paths)
        if (QFileInfo(path).isDir() && !existing.contains(QDir::cleanPath(path)))
            existing << QDir::cleanPath(path);
    return existing;
}

QStringList SpellChecker::availableLanguages(const QStringList &paths) {
    QStringList languages;
    for (const auto &path : paths)
        for (const auto &file : QDir(path).entryInfoList({"*.dic"}, QDir::Files))
            if (QFileInfo::exists(file.absolutePath() + "/" + file.completeBaseName() + ".aff") &&
                !file.completeBaseName().startsWith("hyph") &&
                !languages.contains(file.completeBaseName()))
                languages << file.completeBaseName();
    languages.sort();
    return languages;
}

bool SpellChecker::load(const QString &requested, const QStringList &paths) {
    const auto languages = availableLanguages(paths);
    const auto prefix = requested.section('_', 0, 0) + "_";
    QStringList candidates{requested};
    if (prefix == "en_")
        candidates << "en_US" << "en_GB";
    for (const auto &language : languages)
        if (language.startsWith(prefix))
            candidates << language;
    QString chosen;
    for (const auto &candidate : candidates)
        if (languages.contains(candidate)) {
            chosen = candidate;
            break;
        }
    if (chosen.isEmpty())
        return false;
    for (const auto &path : paths) {
        const auto aff = path + "/" + chosen + ".aff", dic = path + "/" + chosen + ".dic";
        if (!QFileInfo::exists(aff) || !QFileInfo::exists(dic))
            continue;
        auto engine = std::make_unique<Hunspell>(QFile::encodeName(aff).constData(),
                                                 QFile::encodeName(dic).constData());
        const auto encoding = QString::fromStdString(engine->get_dict_encoding());
        QStringEncoder encoder(encoding.toUtf8().constData());
        QStringDecoder decoder(encoding.toUtf8().constData());
        if (encoding.contains("8859-1") || encoding.contains("8859_1")) {
            encoder = QStringEncoder(QStringConverter::Latin1);
            decoder = QStringDecoder(QStringConverter::Latin1);
        } else if (!encoder.isValid() || !decoder.isValid()) {
            encoder = QStringEncoder(QStringConverter::Utf8);
            decoder = QStringDecoder(QStringConverter::Utf8);
        }
        engine_ = std::move(engine);
        encoder_ = std::move(encoder);
        decoder_ = std::move(decoder);
        language_ = chosen;
        UScriptCode codes[8];
        UErrorCode error = U_ZERO_ERROR;
        const auto count = uscript_getCode(chosen.section('_', 0, 0).toUtf8().constData(), codes,
                                           8, &error);
        scripts_.clear();
        if (U_SUCCESS(error))
            scripts_.assign(codes, codes + count);
        // Words added earlier apply to every dictionary.
        if (QFile file(personalPath_); file.open(QIODevice::ReadOnly | QIODevice::Text))
            for (const auto &word : QString::fromUtf8(file.readAll()).split('\n'))
                if (!word.trimmed().isEmpty())
                    engine_->add(encode(word.trimmed()));
        ++generation_;
        return true;
    }
    return false;
}

std::string SpellChecker::encode(const QString &word) const {
    QString normalized = word;
    normalized.replace(QChar(0x2019), '\'');
    return QByteArray(encoder_.encode(normalized)).toStdString();
}

bool SpellChecker::isCorrect(const QString &word) const {
    if (!engine_ || ignored_.contains(word))
        return true;
    return engine_->spell(encode(word));
}

QStringList SpellChecker::suggestions(const QString &word, int limit) const {
    QStringList result;
    if (!engine_)
        return result;
    for (const auto &suggestion : engine_->suggest(encode(word))) {
        result << decoder_.decode(QByteArray::fromStdString(suggestion));
        if (result.size() >= limit)
            break;
    }
    return result;
}

void SpellChecker::addToDictionary(const QString &word) {
    if (word.trimmed().isEmpty())
        return;
    if (engine_)
        engine_->add(encode(word));
    ++generation_;
    if (personalPath_.isEmpty())
        return;
    QDir().mkpath(QFileInfo(personalPath_).absolutePath());
    QFile file(personalPath_);
    if (file.open(QIODevice::Append | QIODevice::Text))
        file.write(word.trimmed().toUtf8() + "\n");
}

void SpellChecker::ignore(const QString &word) {
    ignored_.insert(word);
    ++generation_;
}

void SpellChecker::setPersonalDictionary(const QString &path) {
    personalPath_ = path;
    if (QFile file(path); engine_ && file.open(QIODevice::ReadOnly | QIODevice::Text))
        for (const auto &word : QString::fromUtf8(file.readAll()).split('\n'))
            if (!word.trimmed().isEmpty())
                engine_->add(encode(word.trimmed()));
    ++generation_;
}

std::vector<std::pair<std::size_t, std::size_t>>
SpellChecker::misspelled(const std::u16string &text) const {
    std::vector<std::pair<std::size_t, std::size_t>> result;
    if (!engine_ || text.empty())
        return result;
    const auto *data = reinterpret_cast<const UChar *>(text.data());
    const auto length = static_cast<int32_t>(text.size());
    UErrorCode error = U_ZERO_ERROR;
    std::unique_ptr<UBreakIterator, decltype(&ubrk_close)> words(
        ubrk_open(UBRK_WORD, "und", data, length, &error), ubrk_close);
    if (U_FAILURE(error))
        return result;
    int32_t start = ubrk_first(words.get());
    for (int32_t end = ubrk_next(words.get()); end != UBRK_DONE;
         start = end, end = ubrk_next(words.get())) {
        const auto status = ubrk_getRuleStatus(words.get());
        if (status < UBRK_WORD_LETTER || status >= UBRK_WORD_LETTER_LIMIT || end - start < 2)
            continue;
        bool lower = false, digit = false, otherScript = false;
        for (int32_t i = start; i < end;) {
            UChar32 cp;
            U16_NEXT(data, i, end, cp);
            lower = lower || u_isULowercase(cp);
            digit = digit || u_isdigit(cp);
            UErrorCode scriptError = U_ZERO_ERROR;
            const auto script = uscript_getScript(cp, &scriptError);
            if (!scripts_.empty() && script != USCRIPT_COMMON && script != USCRIPT_INHERITED &&
                std::find(scripts_.begin(), scripts_.end(), script) == scripts_.end())
                otherScript = true;
        }
        if (!lower || digit || otherScript)
            continue;
        const auto word = QString::fromUtf16(text.data() + start, end - start);
        if (!isCorrect(word))
            result.emplace_back(static_cast<std::size_t>(start), static_cast<std::size_t>(end));
    }
    return result;
}
} // namespace wf
