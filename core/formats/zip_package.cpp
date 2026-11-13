#include "core/formats/zip_package.hpp"
#include <memory>
#include <set>
#include <stdexcept>
#include <zip.h>
namespace wf::opc {
namespace {
constexpr std::size_t maxPackage = 16 * 1024 * 1024, maxExpanded = 64 * 1024 * 1024;
using Source = std::unique_ptr<zip_source_t, decltype(&zip_source_free)>;
using Archive = std::unique_ptr<zip_t, decltype(&zip_discard)>;
void validName(const std::string &name) {
    if (name.empty() || name.front() == '/' || name.find('\0') != std::string::npos ||
        name.find('\\') != std::string::npos || name.find(':') != std::string::npos)
        throw std::runtime_error("Unsafe OPC part name");
    for (std::size_t begin = 0; begin < name.size();) {
        auto end = name.find('/', begin);
        auto component = name.substr(begin, end == std::string::npos ? end : end - begin);
        if (component == ".." || component == "." || component.empty())
            throw std::runtime_error("Unsafe OPC part name");
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
}
Archive open(Source &source, int flags) {
    if (!source)
        throw std::runtime_error("Could not allocate ZIP source");
    Archive archive(zip_open_from_source(source.get(), flags, nullptr), &zip_discard);
    if (!archive)
        throw std::runtime_error("Invalid or unsupported DOCX ZIP package");
    source.release(); // libzip owns the source after successful open.
    return archive;
}
} // namespace
Parts read(const std::string &bytes) {
    if (bytes.size() > maxPackage)
        throw std::runtime_error("DOCX exceeds the 16 MiB package limit");
    Source source(zip_source_buffer_create(bytes.data(), bytes.size(), 0, nullptr),
                  &zip_source_free);
    auto archive = open(source, ZIP_RDONLY);
    const auto count = zip_get_num_entries(archive.get(), 0);
    if (count < 0 || count > 4096)
        throw std::runtime_error("DOCX has too many ZIP entries");
    Parts parts;
    std::set<std::string> names;
    std::size_t total = 0;
    for (zip_uint64_t index = 0; index < static_cast<zip_uint64_t>(count); ++index) {
        zip_stat_t stat;
        zip_stat_init(&stat);
        if (zip_stat_index(archive.get(), index, 0, &stat) || !stat.name ||
            !(stat.valid & ZIP_STAT_SIZE) || stat.size > maxPackage ||
            stat.size > maxExpanded - total)
            throw std::runtime_error("DOCX ZIP expansion exceeds supported limits");
        std::string name(stat.name);
        validName(name);
        if (!names.insert(name).second)
            throw std::runtime_error("Duplicate OPC part name");
        total += static_cast<std::size_t>(stat.size);
        if (name.back() == '/')
            continue;
        std::unique_ptr<zip_file_t, decltype(&zip_fclose)> file(
            zip_fopen_index(archive.get(), index, 0), &zip_fclose);
        if (!file)
            throw std::runtime_error("Could not open DOCX ZIP entry");
        auto &data = parts[name];
        data.resize(static_cast<std::size_t>(stat.size));
        std::size_t offset = 0;
        while (offset < data.size()) {
            const auto length = zip_fread(file.get(), data.data() + offset, data.size() - offset);
            if (length <= 0)
                throw std::runtime_error("Truncated DOCX ZIP entry");
            offset += static_cast<std::size_t>(length);
        }
        char extra;
        if (zip_fread(file.get(), &extra, 1) != 0)
            throw std::runtime_error("Invalid DOCX ZIP size or checksum");
    }
    return parts;
}
std::string write(const Parts &parts) {
    if (parts.size() > 4096)
        throw std::runtime_error("Too many OPC parts");
    Source source(zip_source_buffer_create(nullptr, 0, 0, nullptr), &zip_source_free);
    auto *raw = source.get();
    auto archive = open(source, ZIP_CREATE | ZIP_TRUNCATE);
    zip_source_keep(raw);
    Source result(raw, &zip_source_free);
    std::size_t total = 0;
    for (const auto &[name, bytes] : parts) {
        validName(name);
        if (bytes.size() > maxPackage || bytes.size() > maxExpanded - total)
            throw std::runtime_error("OPC content exceeds supported limits");
        total += bytes.size();
        Source entry(zip_source_buffer(archive.get(), bytes.data(), bytes.size(), 0),
                     &zip_source_free);
        if (!entry || zip_file_add(archive.get(), name.c_str(), entry.get(), ZIP_FL_ENC_UTF_8) < 0)
            throw std::runtime_error("Could not create DOCX ZIP entry");
        entry.release();
    }
    if (zip_close(archive.get()))
        throw std::runtime_error("Could not finish DOCX ZIP package");
    archive.release();
    if (zip_source_open(result.get()) || zip_source_seek(result.get(), 0, SEEK_END))
        throw std::runtime_error("Could not read generated DOCX package");
    const auto size = zip_source_tell(result.get());
    if (size < 0 || size > static_cast<zip_int64_t>(maxPackage) ||
        zip_source_seek(result.get(), 0, SEEK_SET))
        throw std::runtime_error("Generated DOCX exceeds supported limits");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto length =
            zip_source_read(result.get(), bytes.data() + offset, bytes.size() - offset);
        if (length <= 0)
            throw std::runtime_error("Could not read generated DOCX package");
        offset += static_cast<std::size_t>(length);
    }
    zip_source_close(result.get());
    return bytes;
}
} // namespace wf::opc
