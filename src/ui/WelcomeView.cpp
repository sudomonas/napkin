#include "WelcomeView.h"
#include "ItemCanvas.h"

#include <QKeyEvent>
#include "Tokens.h"

#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QFontMetrics>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace napkin {
namespace {

using namespace tokens;

constexpr int kLogoSize = 112;

// Measured in characters rather than pixels: "Ctrl+Shift+I" has to fit without
// wrapping at any font size, and the description column has to stay a block
// rather than a ragged edge. Fixed pixel widths did both jobs at the default
// font and neither at 200%.
int keyColumn(const QFont& font)  { return QFontMetrics(font).horizontalAdvance(
                                        QStringLiteral("Ctrl+Shift+I")) + 24; }
int whatColumn(const QFont& font) { return QFontMetrics(font).averageCharWidth() * 28; }

QPixmap loadMark()
{
    // The installed theme icon if there is one, otherwise the copies compiled
    // into the binary — so this works from a build directory too.
    QIcon icon = QIcon::fromTheme(QStringLiteral("io.github.sudomonas.Napkin"));
    if (icon.isNull()) {
        for (const char* size : {"128", "256", "64"})
            icon.addFile(QStringLiteral(":/resources/icons/%1x%1/io.github.sudomonas.Napkin.png").arg(size));
    }
    return icon.pixmap(kLogoSize, kLogoSize);
}

}  // namespace

WelcomeView::WelcomeView(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setAlignment(Qt::AlignCenter);
    outer->setSpacing(0);

    auto* column = new QVBoxLayout;
    column->setAlignment(Qt::AlignHCenter);
    column->setSpacing(0);

    logo_ = new QLabel;
    logo_->setPixmap(loadMark());
    logo_->setAlignment(Qt::AlignCenter);
    column->addWidget(logo_);
    column->addSpacing(18);

    name_ = new QLabel(tr("Napkin"));
    name_->setAlignment(Qt::AlignCenter);
    QFont nameFont = scaledBy(font(), kTypeDisplay, QFont::Medium);
    nameFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    name_->setFont(nameFont);
    column->addWidget(name_);
    column->addSpacing(8);

    tagline_ = new QLabel(tr("A persistent scratch surface for your computer"));
    tagline_->setAlignment(Qt::AlignCenter);
    tagline_->setFont(scaledBy(font(), kTypeLead));
    column->addWidget(tagline_);
    column->addSpacing(4);

    instruction_ = new QLabel(tr("Paste or dump text and images here."));
    instruction_->setAlignment(Qt::AlignCenter);
    column->addWidget(instruction_);
    column->addSpacing(34);

    // Only the keys that get you started. A wall of every binding would be a
    // reference card, and nobody reads a reference card on first run.
    column->addWidget(buildShortcutRow(tr("Ctrl+N"), tr("New napkin"),
                                       SIGNAL(newBufferRequested())), 0, Qt::AlignHCenter);
    column->addWidget(buildShortcutRow(tr("Ctrl+V"), tr("Paste text or an image"),
                                       SIGNAL(pasteRequested())), 0, Qt::AlignHCenter);
    column->addWidget(buildShortcutRow(tr("Ctrl+T"), tr("New note"),
                                       SIGNAL(newTextRequested())), 0, Qt::AlignHCenter);
    column->addWidget(buildShortcutRow(tr("Ctrl+Shift+I"), tr("Add an image from a file"),
                                       SIGNAL(addImageRequested())), 0, Qt::AlignHCenter);
    column->addWidget(buildShortcutRow(tr("Ctrl+F"), tr("Search everything"),
                                       SIGNAL(searchRequested())), 0, Qt::AlignHCenter);

    // The keys above get you started; the rest — Pin, Keep, Calculate, the
    // Trash's undo — is invisible until someone shows you, so offer to.
    column->addSpacing(22);
    auto* tour = new QPushButton(tr("Take the tour"));
    tour->setObjectName(QStringLiteral("tourButton"));
    tour->setProperty("napkinShape", QStringLiteral("pill"));
    tour->setCursor(Qt::PointingHandCursor);
    connect(tour, &QPushButton::clicked, this, &WelcomeView::tourRequested);
    column->addWidget(tour, 0, Qt::AlignHCenter);

    column->addSpacing(34);
    footer_ = new QLabel(tr("Nothing here needs a name, a folder or a tag.\n"
                            "Everything stays on this machine."));
    footer_->setAlignment(Qt::AlignCenter);
    footer_->setFont(scaledBy(font(), kTypeCaption));
    column->addWidget(footer_);

    outer->addLayout(column);
    applyPalette();
}

QWidget* WelcomeView::buildShortcutRow(const QString& keys, const QString& what,
                                       const char* signalName)
{
    // A row, not a label: reading what a key does and pressing it should be the
    // same gesture on the screen that exists to teach you the keys.
    auto* row = new QPushButton;
    row->setFlat(true);
    row->setCursor(Qt::PointingHandCursor);
    row->setAccessibleName(QStringLiteral("%1 — %2").arg(keys, what));
    row->setFocusPolicy(Qt::TabFocus);

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 5, 10, 5);
    layout->setSpacing(0);

    auto* key = new QLabel(keys);
    QFont keyFont = row->font();
    keyFont.setFamilies({QStringLiteral("monospace")});
    key->setFont(keyFont);
    key->setFixedWidth(keyColumn(keyFont));
    key->setObjectName(QStringLiteral("shortcutKey"));

    auto* text = new QLabel(what);
    text->setFixedWidth(whatColumn(text->font()));
    text->setObjectName(QStringLiteral("shortcutWhat"));

    layout->addWidget(key);
    layout->addWidget(text);

    // Sized to its content rather than stretched: a row that expands to the
    // window width cannot be centred with the title above it, and the block
    // ended up hard against the left edge.
    row->setFixedWidth(key->width() + text->width() + 20);

    connect(row, SIGNAL(clicked()), this, signalName);
    return row;
}

void WelcomeView::applyPalette()
{
    const QPalette pal = QGuiApplication::palette();
    // Tint the role the label actually paints with, not the one we assume it
    // paints with. A QLabel inherits its parent's foreground role, so the ones
    // inside the clickable shortcut rows draw with ButtonText; setting
    // WindowText on those changed nothing, and they stayed whatever the
    // platform's ButtonText happened to be — white text on a light window.
    auto tint = [&](QLabel* label, int alpha) {
        if (!label) return;
        QPalette p = label->palette();
        p.setColor(label->foregroundRole(), text(pal, alpha));
        label->setPalette(p);
    };

    tint(name_, kTextPrimary);
    tint(tagline_, kTextSecondary);
    tint(instruction_, kTextSecondary);
    tint(footer_, kTextTertiary);

    // The key is the thing the eye should land on; its description is quieter.
    for (QLabel* label : findChildren<QLabel*>(QStringLiteral("shortcutKey")))
        tint(label, kTextPrimary);
    for (QLabel* label : findChildren<QLabel*>(QStringLiteral("shortcutWhat")))
        tint(label, kTextSecondary);
}

void WelcomeView::changeEvent(QEvent* e)
{
    // Colours captured here would otherwise go stale on a theme change, exactly
    // as they did on the cards.
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::ApplicationPaletteChange) {
        logo_->setPixmap(loadMark());
        applyPalette();
    }
    if (e->type() == QEvent::FontChange || e->type() == QEvent::ApplicationFontChange) {
        // The shortcut rows are sized in characters, so a new face or size
        // means new widths — otherwise the key column clips "Ctrl+Shift+I".
        for (QLabel* key : findChildren<QLabel*>(QStringLiteral("shortcutKey")))
            key->setFixedWidth(keyColumn(key->font()));
        for (QLabel* what : findChildren<QLabel*>(QStringLiteral("shortcutWhat")))
            what->setFixedWidth(whatColumn(what->font()));
        for (QWidget* row : findChildren<QPushButton*>())
            row->setFixedWidth(row->layout() ? row->layout()->sizeHint().width() : row->width());
    }
    QWidget::changeEvent(e);
}

// Letters reach here from whichever row button has focus, since a button only
// takes Space and Enter. Before this they vanished — until the first Space
// "clicked" the focused New napkin row and made an empty napkin, which is what
// a first-time user saw when trying to type on this page.
void WelcomeView::keyPressEvent(QKeyEvent* e)
{
    if (ItemCanvas::isTyping(e)) { emit textTyped(e->text()); return; }
    QWidget::keyPressEvent(e);
}

}  // namespace napkin
