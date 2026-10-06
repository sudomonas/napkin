#include "Exporter.h"
#include "../data/BufferRepository.h"
#include "../data/ItemRepository.h"
#include "BlobStore.h"
#include "ImageFormats.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace napkin {
namespace {

QString isoTime(Timestamp ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC).toString(Qt::ISODate);
}

// Names that mean something to the filesystem rather than to the user. Reserved
// on Windows even without an extension, and Napkin is meant to reach Windows in
// Phase 9 — a folder exported on Linux must not be one that cannot be unpacked
// there.
bool isReservedName(const QString& name)
{
    static const QStringList kReserved = {
        QStringLiteral("con"), QStringLiteral("prn"), QStringLiteral("aux"),
        QStringLiteral("nul"), QStringLiteral("com1"), QStringLiteral("com2"),
        QStringLiteral("com3"), QStringLiteral("com4"), QStringLiteral("com5"),
        QStringLiteral("com6"), QStringLiteral("com7"), QStringLiteral("com8"),
        QStringLiteral("com9"), QStringLiteral("lpt1"), QStringLiteral("lpt2"),
        QStringLiteral("lpt3"), QStringLiteral("lpt4"), QStringLiteral("lpt5"),
        QStringLiteral("lpt6"), QStringLiteral("lpt7"), QStringLiteral("lpt8"),
        QStringLiteral("lpt9")};
    return kReserved.contains(name.toLower());
}

// A folder that does not already exist, so an export never overwrites an
// earlier one. Napkin's whole lifecycle story is that it does not destroy what
// you did not ask it to.
QString uniqueDir(const QString& parent, const QString& wanted)
{
    QDir base(parent);
    QString name = wanted;
    for (int n = 2; base.exists(name) && n < 10000; ++n)
        name = QStringLiteral("%1-%2").arg(wanted).arg(n);
    return base.filePath(name);
}

}  // namespace

Exporter::Exporter(BufferRepository& buffers, ItemRepository& items, BlobStore& blobs)
    : buffers_(buffers), items_(items), blobs_(blobs) {}

// The only place user content is allowed to become a filename.
//
// Everything outside the allow-list goes, which handles path separators, "..",
// NUL, newlines, control characters and RTL overrides in one rule rather than
// in a list of special cases that has to stay complete.
QString Exporter::slug(const QString& text, int maxChars)
{
    QString out;
    out.reserve(maxChars);
    bool lastWasDash = true;   // so a leading separator is dropped
    for (const QChar c : text) {
        if (out.size() >= maxChars) break;
        if (c.isLetterOrNumber()) {
            out.append(c.toLower());
            lastWasDash = false;
        } else if (!lastWasDash) {
            out.append(u'-');
            lastWasDash = true;
        }
    }
    while (out.endsWith(u'-')) out.chop(1);
    if (out.isEmpty() || isReservedName(out)) return QStringLiteral("item");
    return out;
}

bool Exporter::writeBuffer(BufferId id, const QString& parentDir, Result& result)
{
    const auto buffer = buffers_.find(id);
    if (!buffer) {
        result.problems << QObject::tr("Napkin %1 no longer exists.").arg(qint64(id));
        return false;
    }
    // Chronological, not the board's order. listForBuffer() sorts newest-first
    // because that is how the board reads; a folder of files is read from 001
    // downwards, and an export that numbered the newest item 001 would invert
    // every buffer on its way out.
    auto contents = items_.listForBuffer(id);
    std::sort(contents.begin(), contents.end(), [](const Item& a, const Item& b) {
        return a.position != b.position ? a.position < b.position : a.id < b.id;
    });

    // Named from the first text in it, so a folder of exports is browsable
    // without opening anything. The id keeps two similar buffers apart.
    QString label;
    if (const auto buffer = buffers_.find(id); buffer && !buffer->name.isEmpty())
        label = slug(buffer->name, 40);
    for (const Item& item : contents) {
        if (!label.isEmpty()) break;
        if (item.type == ItemType::Text && !item.text.trimmed().isEmpty()) {
            label = slug(item.text, 40);
            break;
        }
    }
    if (label.isEmpty()) label = QStringLiteral("images");

    const QString dir = uniqueDir(parentDir, QStringLiteral("%1-%2")
                                                 .arg(qint64(id), 4, 10, QChar(u'0'))
                                                 .arg(label));
    if (!QDir().mkpath(dir)) {
        result.problems << QObject::tr("Could not create %1.").arg(dir);
        return false;
    }

    QJsonArray manifestItems;
    int index = 0;
    for (const Item& item : contents) {
        ++index;
        QJsonObject entry;
        entry["position"] = item.position;
        entry["created_at"] = isoTime(item.createdAt);
        entry["modified_at"] = isoTime(item.modifiedAt);

        if (item.type == ItemType::Text) {
            const QString name = QStringLiteral("%1-%2.txt")
                                     .arg(index, 3, 10, QChar(u'0'))
                                     .arg(slug(item.text, 32));
            QSaveFile file(QDir(dir).filePath(name));
            // Not QIODevice::Text: that turns every \n into \r\n on Windows,
            // so the file would no longer be the text that was typed, and an
            // export made there would differ from the same export made here.
            if (!file.open(QIODevice::WriteOnly)
                || file.write(item.text.toUtf8()) < 0 || !file.commit()) {
                result.problems << QObject::tr("Could not write %1.").arg(name);
                continue;
            }
            entry["type"] = QStringLiteral("text");
            entry["file"] = name;
            entry["characters"] = item.text.size();
            result.bytes += item.text.toUtf8().size();
        } else {
            const QString source = blobs_.pathFor(item.blobHash, item.mime);
            const QString name = QStringLiteral("%1-%2.%3")
                                     .arg(index, 3, 10, QChar(u'0'))
                                     .arg(item.sourceName.isEmpty()
                                              ? QStringLiteral("image")
                                              : slug(item.sourceName, 32))
                                     .arg(formats::extensionFor(item.mime));
            // Copied verbatim. An export that re-encoded would hand back
            // something that is not what was stored, and the manifest's hash
            // would stop being checkable against it.
            if (!QFile::exists(source)) {
                result.problems
                    << QObject::tr("The image for %1 is missing from storage.").arg(name);
                continue;
            }
            if (!QFile::copy(source, QDir(dir).filePath(name))) {
                result.problems << QObject::tr("Could not copy %1.").arg(name);
                continue;
            }
            entry["type"] = QStringLiteral("image");
            entry["file"] = name;
            entry["mime"] = item.mime;
            entry["sha256"] = item.blobHash;
            entry["width"] = item.width;
            entry["height"] = item.height;
            entry["bytes"] = double(item.byteSize);
            entry["animated"] = item.animated;
            if (!item.sourceName.isEmpty()) entry["source_name"] = item.sourceName;
            result.bytes += item.byteSize;
            ++result.images;
        }
        manifestItems.append(entry);
        ++result.items;
    }

    QJsonObject manifest;
    manifest["napkin_export_version"] = 1;
    manifest["exported_at"] = isoTime(QDateTime::currentMSecsSinceEpoch());
    QJsonObject meta;
    meta["id"] = double(qint64(id));
    meta["created_at"] = isoTime(buffer->createdAt);
    meta["modified_at"] = isoTime(buffer->modifiedAt);
    meta["pinned"] = buffer->pinned;
    meta["kept"] = buffer->kept;
    if (buffer->deletedAt) meta["deleted_at"] = isoTime(*buffer->deletedAt);
    manifest["buffer"] = meta;
    manifest["items"] = manifestItems;

    QSaveFile mf(QDir(dir).filePath(QStringLiteral("manifest.json")));
    if (!mf.open(QIODevice::WriteOnly)
        || mf.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) < 0
        || !mf.commit()) {
        result.problems << QObject::tr("Could not write the manifest for %1.").arg(dir);
        return false;
    }

    ++result.buffers;
    if (result.rootDir.isEmpty()) result.rootDir = dir;
    return true;
}

Exporter::Result Exporter::exportBuffer(BufferId id, const QString& destination)
{
    Result result;
    if (!QDir().mkpath(destination)) {
        result.error = QObject::tr("Napkin could not write to %1.").arg(destination);
        return result;
    }
    result.ok = writeBuffer(id, destination, result);
    if (!result.ok && result.error.isEmpty())
        result.error = QObject::tr("Nothing could be exported.");
    return result;
}

Exporter::Result Exporter::exportAll(const QString& destination)
{
    Result result;
    const QString root = uniqueDir(
        destination, QStringLiteral("napkin-export-%1")
                         .arg(QDateTime::currentDateTime().toString(
                             QStringLiteral("yyyy-MM-dd"))));
    if (!QDir().mkpath(root)) {
        result.error = QObject::tr("Napkin could not write to %1.").arg(destination);
        return result;
    }
    result.rootDir = root;

    // Windowed, like everything else that walks the whole stack: an export of
    // 5000 buffers must not build a 5000-element vector of metadata first.
    constexpr int kPage = 200;
    for (int offset = 0;; offset += kPage) {
        const auto page = buffers_.listLive(kPage, offset);
        if (page.empty()) break;
        for (const Buffer& buffer : page) writeBuffer(buffer.id, root, result);
        if (int(page.size()) < kPage) break;
    }

    result.rootDir = root;   // writeBuffer sets it to the first child; keep the root
    result.ok = result.buffers > 0;
    if (!result.ok && result.error.isEmpty())
        result.error = QObject::tr("There is nothing to export yet.");
    return result;
}

}  // namespace napkin
