#pragma once
#include <QSet>
#include <QStringConverter>
#include <QStringList>
#include <memory>
#include <string>
#include <utility>
#include <vector>
class Hunspell;
namespace wf {
// Spell checking with Hunspell and locally installed dictionaries. Nothing leaves the
// computer. Words added by the user go to a personal word list on disk.
class SpellChecker {
  public:
    SpellChecker();
    ~SpellChecker();
    SpellChecker(const SpellChecker &) = delete;
    SpellChecker &operator=(const SpellChecker &) = delete;
    // The application-wide checker; on first use it loads the dictionary closest to
    // the system language.
    static SpellChecker &shared();
    // Folders searched for <language>.aff/.dic pairs.
    static QStringList searchPaths();
    static QStringList availableLanguages(const QStringList &paths = searchPaths());
    // Loads `language` (e.g. "en_IN"), else the same language in another region
    // (preferring en_US/en_GB for English), else nothing.
    bool load(const QString &language, const QStringList &paths = searchPaths());
    bool available() const {
        return engine_ != nullptr;
    }
    QString language() const {
        return language_;
    }
    bool isCorrect(const QString &word) const;
    QStringList suggestions(const QString &word, int limit = 5) const;
    void addToDictionary(const QString &word);
    void ignore(const QString &word);
    // Where added words are stored; existing words in the file are loaded.
    void setPersonalDictionary(const QString &path);
    // Misspelled word ranges (UTF-16 offsets) in one paragraph of text. Words in
    // another script than the dictionary's, words with digits, single letters and
    // words in capitals (usually acronyms) are not checked.
    std::vector<std::pair<std::size_t, std::size_t>> misspelled(const std::u16string &) const;
    // Changes whenever results could change (dictionary loaded, word added/ignored).
    std::uint64_t generation() const {
        return generation_;
    }

  private:
    std::unique_ptr<Hunspell> engine_;
    QString language_, personalPath_;
    QSet<QString> ignored_;
    std::vector<int> scripts_;
    mutable QStringEncoder encoder_;
    mutable QStringDecoder decoder_;
    std::uint64_t generation_ = 1;
    bool loadAttempted_ = false;
    std::string encode(const QString &) const;
};
} // namespace wf
