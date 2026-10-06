#include "Preview.h"
#include "Links.h"
#include <QObject>
#include <QStringList>

namespace napkin {
namespace {

QStringList nonBlankLines(const QString& text, int limit)
{
    QStringList out;
    // Scanning by index rather than split() so a huge single-line paste does not
    // allocate a copy of itself before we throw all but the first 256 chars away.
    qsizetype pos = 0;
    while (pos < text.size() && out.size() < limit) {
        qsizetype end = text.indexOf(QLatin1Char('\n'), pos);
        if (end < 0) end = text.size();
        const QString line = text.mid(pos, std::min(end - pos, qsizetype(kPreviewLineLimit) * 4))
                                 .trimmed();
        if (!line.isEmpty()) out << line.left(kPreviewLineLimit);
        pos = end + 1;
    }
    return out;
}

QString imageLabel(const Item& item)
{
    // A pasted image has no filename; one added through the picker does. Not
    // "Screenshot": calling a photo a screenshot is interpreting it, which §1
    // rules out — and the word was then unsearchable, since it was never stored.
    return item.sourceName.isEmpty() ? QObject::tr("Image") : item.sourceName;
}

}  // namespace

QString firstLine(const QString& text)
{
    const auto lines = nonBlankLines(text, 1);
    return lines.isEmpty() ? QString() : lines.first();
}

QString formatBytes(qint64 bytes)
{
    constexpr qint64 kb = 1024, mb = kb * 1024, gb = mb * 1024;
    if (bytes >= gb) return QStringLiteral("%1 GB").arg(double(bytes) / gb, 0, 'f', 1);
    if (bytes >= mb) return QStringLiteral("%1 MB").arg(double(bytes) / mb, 0, 'f', 1);
    if (bytes >= kb) return QStringLiteral("%1 KB").arg(bytes / kb);
    return QStringLiteral("%1 B").arg(bytes);
}

BufferPreview derivePreview(const std::vector<Item>& head, int totalCount, int imageCount,
                            const std::vector<Item>& latest, const QString& name)
{
    BufferPreview p;
    p.itemCount = totalCount;
    p.imageCount = imageCount;

    for (const auto& i : head) {
        if (i.type != ItemType::Image) continue;
        if (int(p.thumbs.size()) >= kMaxCardThumbs) break;
        p.thumbs.push_back({i.blobHash, i.mime, i.animated});
    }

    if (head.empty()) {
        p.primary = name;
        return p;
    }

    // The title comes from the first NOTE, then the first link, then the first
    // image. A napkin that began with a picture or a URL was titled "Image" or
    // by the raw address, even when a note on it said what it was about — and
    // Export, which names folders from text, called the same napkin something
    // sensible (second usability test, 2026-09-19).
    const Item* note = nullptr;
    const Item* link = nullptr;
    for (const auto& i : head) {
        if (i.type != ItemType::Text || nonBlankLines(i.text, 1).isEmpty()) continue;
        if (links::soleUrl(i.text)) { if (!link) link = &i; }
        else { note = &i; break; }
    }
    const Item& first = note ? *note : link ? *link : head.front();
    if (&first == link) {
        p.primary = links::displayForm(*links::soleUrl(first.text));
    } else if (first.type == ItemType::Text) {
        const auto lines = nonBlankLines(first.text, 2);
        if (!lines.isEmpty()) p.primary = lines.first();
        if (lines.size() > 1)  p.secondary = lines.at(1);
    } else {
        // Several unnamed images lead with the count; one leads with its name.
        p.primary = (first.sourceName.isEmpty() && imageCount > 1)
            ? QObject::tr("%1 images").arg(imageCount)
            : imageLabel(first);
        if (first.width > 0 && first.height > 0)
            p.secondary = QStringLiteral("%1 × %2").arg(first.width).arg(first.height);
    }

    // A multi-item buffer says so, in place of whatever detail the first item
    // offered — the count is the more useful fact. Unless the buffer is nothing
    // but images, in which case "5 items" merely repeats "5 images".
    const bool allImages = imageCount == totalCount;
    if (totalCount > 1 && !allImages)
        p.secondary = QObject::tr("%1 items").arg(totalCount);

    // A name the user gave is the title, whatever the contents would say.
    if (!name.isEmpty()) p.primary = name;

    // What was added last, unless that is what the title already says.
    const ItemId titledBy = name.isEmpty() ? first.id : kNoItem;
    if (totalCount > 1) {
        for (const auto& i : latest) {
            if (i.id == titledBy) continue;
            if (i.type == ItemType::Text) {
                if (const auto url = links::soleUrl(i.text)) p.latest = links::displayForm(*url);
                else p.latest = firstLine(i.text);
            } else {
                p.latest = imageLabel(i);
            }
            if (!p.latest.isEmpty()) break;
        }
    }

    // A buffer holding only untitled images still needs a primary line.
    if (p.primary.isEmpty() && p.hasImage()) {
        p.primary = imageCount == 1 ? QObject::tr("Image")
                                    : QObject::tr("%1 images").arg(imageCount);
    }

    return p;
}

}  // namespace napkin
