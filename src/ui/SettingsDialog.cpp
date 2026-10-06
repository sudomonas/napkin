#include "SettingsDialog.h"
#include "../app/GlobalShortcut.h"
#include "../domain/BufferService.h"
#include "Tokens.h"
#include "NapkinStyle.h"
#include "SurfacePanel.h"
#include "TrayIcon.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QCheckBox>
#include <QGroupBox>
#include <QIcon>
#include <QPixmap>
#include <QLabel>
#include <QSettings>
#include <QSpinBox>
#include <QFontComboBox>
#include <QStyleFactory>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QListWidget>
#include <QStyleHints>
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#include <QAccessibilityHints>
#endif
#include <QTimer>
#include <QEvent>
#include <QStyleHints>
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#include <QAccessibilityHints>
#endif
#include <QStyle>
#include <algorithm>
#include <QVBoxLayout>

namespace napkin {
namespace {

constexpr auto kTheme = "appearance/theme";
constexpr auto kFontFamily = "appearance/fontFamily";
constexpr auto kTextScale = "appearance/textScalePercent";
constexpr auto kAccent = "appearance/accent";
constexpr auto kKeepInTray = "behaviour/keepInTray";
constexpr auto kCaptureShortcut = "behaviour/captureShortcut";
constexpr auto kOlder = "lifecycle/olderThanDays";
constexpr auto kRetention = "lifecycle/trashRetentionDays";

// Kept for the life of the process so the "System" choice can be restored
// without asking the platform again.
QPalette& systemPalette()
{
    static QPalette saved = QApplication::palette();
    return saved;
}

// Same reasoning for the font: once Napkin has overridden it, the platform's
// own choice is no longer readable back off QApplication.
QFont& systemFont()
{
    static QFont saved = QApplication::font();
    return saved;
}

// The platform's own widget style, for the same reason: once Napkin has
// replaced it there is nothing left to ask.
QString& systemStyle()
{
    static QString saved = QApplication::style()->name();
    return saved;
}

// When the desktop asks for high contrast, Napkin's look steps aside entirely —
// the platform's style and palette, unmodified. An own look is a preference;
// high contrast is a need (SPEC.md §14). Readable only from Qt 6.10.
bool highContrast()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    return QGuiApplication::styleHints()->accessibility()->contrastPreference()
           == Qt::ContrastPreference::HighContrast;
#else
    return false;
#endif
}

// Napkin's own style on every platform (SPEC.md §7, "The design system").
// It replaced a Windows-only switch to Fusion in dark mode: windowsvista
// ignores the palette, so dark mode left every control light — a platform
// style deciding what Napkin looks like, which is now never the case.
void applyStyle()
{
    if (highContrast()) {
        if (qobject_cast<NapkinStyle*>(QApplication::style()))
            if (QStyle* style = QStyleFactory::create(systemStyle())) QApplication::setStyle(style);
        return;
    }
    if (!qobject_cast<NapkinStyle*>(QApplication::style()))
        QApplication::setStyle(new NapkinStyle);
}

// Whether the desktop is dark, for "follow the system". The colour scheme when
// the platform says; otherwise the platform's own palette, read before Napkin
// replaced it.
bool systemIsDark()
{
    switch (QGuiApplication::styleHints()->colorScheme()) {
    case Qt::ColorScheme::Dark:  return true;
    case Qt::ColorScheme::Light: return false;
    default: return !tokens::isLightTheme(systemPalette());
    }
}

// Inter, bundled, so text looks the same everywhere. Registered once; if the
// resource cannot be read the platform's face is used and nothing else changes.
QString napkinFamily()
{
    static const QString family = [] {
        QString found;
        for (const char* weight : {"Regular", "Medium", "SemiBold", "Bold"}) {
            const int id = QFontDatabase::addApplicationFont(
                QStringLiteral(":/resources/fonts/inter/Inter-%1.ttf").arg(QLatin1String(weight)));
            if (id >= 0 && found.isEmpty())
                found = QFontDatabase::applicationFontFamilies(id).value(0);
        }
        return found;
    }();
    return family;
}

// The size everything is scaled from: the body size the mockup was drawn at,
// unless the desktop asks for larger. A platform default of 9pt (Windows) made
// every card a size smaller than the same card on KDE; a desktop set larger
// for legibility is still honoured.
qreal basePointSize()
{
    return std::max(tokens::kBodyPointSize, systemFont().pointSizeF());
}

QFont baseFont(const QString& family, int percent)
{
    QFont font = systemFont();
    const QString face = family.isEmpty() ? napkinFamily() : family;
    if (!face.isEmpty()) font.setFamilies({face});
    font.setPointSizeF(std::max(tokens::kMinPointSize, basePointSize() * percent / 100.0));
    return font;
}

// A short, named set rather than a colour wheel. Napkin is not a theming
// engine, and every one of these is a saturated hue that stays above the 3:1
// floor against both a light and a dark surface once readableAccent() has
// hardened it.
struct NamedAccent { const char* name; QRgb rgb; };
const NamedAccent kAccents[] = {
    {QT_TRANSLATE_NOOP("SettingsDialog", "Follow the system"), 0},
    {QT_TRANSLATE_NOOP("SettingsDialog", "Blue"),   0xff3daee9},
    {QT_TRANSLATE_NOOP("SettingsDialog", "Violet"), 0xff8e6fd8},
    {QT_TRANSLATE_NOOP("SettingsDialog", "Green"),  0xff27ae60},
    {QT_TRANSLATE_NOOP("SettingsDialog", "Amber"),  0xffd88c1a},
    {QT_TRANSLATE_NOOP("SettingsDialog", "Red"),    0xffda4453},
    {QT_TRANSLATE_NOOP("SettingsDialog", "Slate"),  0xff5d7285},
};
constexpr int kAccentCount = int(sizeof(kAccents) / sizeof(kAccents[0]));

const int kScales[] = {80, 90, 100, 110, 125, 150, 175, 200};
constexpr int kScaleCount = int(sizeof(kScales) / sizeof(kScales[0]));

// A theme is every role or it is none of them.
//
// This used to patch four roles — Window, Base, Text, WindowText — onto
// whatever the platform handed us. That works only when the platform already
// agrees about light or dark. Running a dark desktop and choosing Light left
// ButtonText at white, so the menu bar, the header buttons and the shortcut
// rows on the start page were white text on a light window; choosing Dark under
// a light desktop had the mirror-image fault. Every role a widget can draw with
// is named here.
QPalette buildPalette(bool dark, const QPalette& system)
{
    QPalette p;
    auto both = [&](QPalette::ColorRole role, QColor c) { p.setColor(role, c); };

    const tokens::ThemeColors& c = dark ? tokens::kDarkColors : tokens::kLightColors;
    both(QPalette::Window,        QColor::fromRgb(c.window));
    both(QPalette::WindowText,    QColor::fromRgb(c.windowText));
    both(QPalette::Base,          QColor::fromRgb(c.base));
    both(QPalette::AlternateBase, QColor::fromRgb(c.alternateBase));
    both(QPalette::Text,          QColor::fromRgb(c.text));
    both(QPalette::Button,        QColor::fromRgb(c.button));
    both(QPalette::ButtonText,    QColor::fromRgb(c.buttonText));
    both(QPalette::BrightText,    QColor::fromRgb(c.brightText));
    both(QPalette::ToolTipBase,   QColor::fromRgb(c.toolTipBase));
    both(QPalette::ToolTipText,   QColor::fromRgb(c.toolTipText));
    both(QPalette::Light,         QColor::fromRgb(c.light));
    both(QPalette::Midlight,      QColor::fromRgb(c.midlight));
    both(QPalette::Mid,           QColor::fromRgb(c.mid));
    both(QPalette::Dark,          QColor::fromRgb(c.dark));
    both(QPalette::Shadow,        QColor::fromRgb(c.shadow));
    both(QPalette::Link,          QColor::fromRgb(c.link));
    both(QPalette::LinkVisited,   QColor::fromRgb(c.linkVisited));

    // Placeholders are text and get no contrast exemption (SPEC.md §7), so this
    // is the same alpha the tokens use for the quietest readable text.
    QColor placeholder = p.color(QPalette::Text);
    placeholder.setAlpha(tokens::kTextTertiary);
    both(QPalette::PlaceholderText, placeholder);

    // The accent is the user's, not ours — either the one they picked here or
    // the platform's, and a saturated accent reads on either background. Its
    // partner is chosen below rather than inherited, because a light-theme
    // HighlightedText carried into a dark theme is how selected text disappears.
    const QColor chosen = SettingsDialog::accent();
    const QColor accent = chosen.isValid() ? chosen : system.color(QPalette::Highlight);
    both(QPalette::Highlight, accent);
    both(QPalette::HighlightedText, tokens::textOn(accent));

    // Disabled is a group, not a role: without it Qt keeps the enabled colour
    // and nothing looks disabled.
    const QColor greyed = QColor::fromRgb(c.disabledText);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text,
                                     QPalette::ButtonText, QPalette::HighlightedText})
        p.setColor(QPalette::Disabled, role, greyed);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor::fromRgb(c.disabledHighlight));

    return p;
}

}  // namespace

void SettingsDialog::setTheme(Theme t)
{
    QSettings().setValue(kTheme, int(t));
    applyAppearance();
}

SettingsDialog::Theme SettingsDialog::theme()
{
    return Theme(QSettings().value(kTheme, int(Theme::System)).toInt());
}

QString SettingsDialog::fontFamily()
{
    return QSettings().value(kFontFamily, QString()).toString();
}

int SettingsDialog::textScalePercent()
{
    const int stored = QSettings().value(kTextScale, 100).toInt();
    return std::clamp(stored, 50, 300);   // a corrupt setting must not be unreadable
}

QColor SettingsDialog::accent()
{
    const QString stored = QSettings().value(kAccent, QString()).toString();
    if (stored.isEmpty()) return {};
    const QColor colour(stored);
    return colour.isValid() ? colour : QColor();
}

bool SettingsDialog::captureShortcut()
{
    return QSettings().value(kCaptureShortcut, false).toBool() && GlobalShortcut::isSupported();
}

bool SettingsDialog::keepInTray()
{
    // A desktop with no tray cannot honour this however it is stored, and
    // answering true there would let the window close to somewhere that does
    // not exist.
    return QSettings().value(kKeepInTray, false).toBool()
           && TrayIcon::availableOnThisDesktop();
}

int SettingsDialog::olderThanDays()
{
    return QSettings().value(kOlder, kOlderThresholdDays).toInt();
}

int SettingsDialog::trashRetentionDays()
{
    return QSettings().value(kRetention, kTrashRetentionDays).toInt();
}

void SettingsDialog::applyAppearance()
{
    // Capture the platform's own choices before overriding any of them.
    systemPalette();
    systemFont();
    systemStyle();

    // Before the palette: a style change can hand widgets its own standard
    // palette, and the one Napkin sets must be the last word.
    applyStyle();

    // "Follow the system" follows its light or dark, in Napkin's own colours.
    // It used to mean the platform's palette, so the same Napkin was Breeze on
    // KDE, Adwaita-ish on GNOME and grey Fusion on Windows.
    if (highContrast()) {
        QApplication::setPalette(systemPalette());
    } else {
        const bool dark = theme() == Theme::Dark || (theme() == Theme::System && systemIsDark());
        QApplication::setPalette(buildPalette(dark, systemPalette()));
    }

    // Scaled from the base size, never from the current one: scaling the
    // already-scaled font would compound every time this ran.
    QApplication::setFont(baseFont(fontFamily(), textScalePercent()));
}

namespace {

// systemPalette() is captured once and Napkin's own palette then hides the
// platform's, so a desktop that switched to dark while Napkin ran was never
// seen: the window kept the old scheme until a restart. Resetting the
// application palette hands back the platform's current one to re-capture.
void refreshFromPlatform()
{
    QApplication::setPalette(QPalette());
    systemPalette() = QApplication::palette();
    SettingsDialog::applyAppearance();
}

// Every widget receives ThemeChange, so one desktop change arrives as dozens of
// events; they collapse into one refresh on the next turn of the event loop,
// by which time the platform has finished updating everything it will.
class ThemeWatcher : public QObject {
public:
    explicit ThemeWatcher(QObject* parent) : QObject(parent)
    {
        pending_.setSingleShot(true);
        pending_.setInterval(0);
        connect(&pending_, &QTimer::timeout, this, &refreshFromPlatform);
        // A light/dark flip, even if no widget exists to be told of it.
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                &pending_, qOverload<>(&QTimer::start));
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        // High contrast switched on or off: Napkin's look steps aside or back.
        connect(QGuiApplication::styleHints()->accessibility(),
                &QAccessibilityHints::contrastPreferenceChanged,
                &pending_, qOverload<>(&QTimer::start));
#endif
    }

protected:
    bool eventFilter(QObject* watched, QEvent* e) override
    {
        // Any other change of theme or colour scheme on the desktop.
        if (e->type() == QEvent::ThemeChange) pending_.start();
        return QObject::eventFilter(watched, e);
    }

private:
    QTimer pending_;
};

}  // namespace

void SettingsDialog::followSystemChanges()
{
    static ThemeWatcher* watcher = nullptr;
    if (watcher) return;
    watcher = new ThemeWatcher(qApp);
    qApp->installEventFilter(watcher);
}

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Settings"));

    // Two panes, as KDE's System Settings has them: the pages on the left in a
    // surface like the main window's sidebar, the chosen page on the right.
    // One long column had grown to three groups and four paragraphs of notes.
    //
    // Exactly as large as the largest page, and not resizable: Qt under-reported
    // the old column's minimum (539px for 607px of content), so it opened
    // squeezed and clipped the preview and the notes (usability test,
    // 2026-09-19). Every page is laid out at that one size.
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(tokens::kWindowMargin, tokens::kWindowMargin,
                              tokens::kWindowMargin, tokens::kWindowMargin);
    outer->setSpacing(tokens::kWindowMargin);
    outer->setSizeConstraint(QLayout::SetFixedSize);
    auto* panes = new QHBoxLayout;
    panes->setSpacing(tokens::kHeaderGap);
    outer->addLayout(panes);

    auto* navPanel = new SurfacePanel;
    auto* navColumn = new QVBoxLayout(navPanel);
    navColumn->setContentsMargins(tokens::kGapTight, tokens::kGapTight, tokens::kGapTight, tokens::kGapTight);
    auto* nav = new QListWidget;
    nav->setObjectName(QStringLiteral("settingsPages"));
    nav->setProperty(NapkinStyle::kShapeProperty, QStringLiteral("nav"));
    nav->setFrameShape(QFrame::NoFrame);
    nav->setAutoFillBackground(false);
    nav->viewport()->setAutoFillBackground(false);
    nav->setFixedWidth(200);
    nav->setAccessibleName(tr("Settings pages"));
    navColumn->addWidget(nav);
    panes->addWidget(navPanel);

    auto* pages = new QStackedWidget;
    pages->setObjectName(QStringLiteral("settingsStack"));
    panes->addWidget(pages, 1);
    connect(nav, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);

    // A page: its name as a heading, then its groups.
    auto addPage = [&](const QString& title) {
        auto* item = new QListWidgetItem(title, nav);
        item->setSizeHint(QSize(0, tokens::kHeaderControlH));
        auto* page = new QWidget;
        auto* column = new QVBoxLayout(page);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(tokens::kWindowMargin);
        auto* heading = new QLabel(title);
        heading->setFont(tokens::scaledBy(heading->font(), tokens::kTypeTitle, QFont::DemiBold));
        column->addWidget(heading);
        pages->addWidget(page);
        return column;
    };
    auto* appearancePage = addPage(tr("Appearance"));
    auto* napkinsPage    = addPage(tr("Napkins & trash"));
    auto* backgroundPage = addPage(tr("In the background"));
    setMinimumWidth(720);

    // --- appearance ---------------------------------------------------------
    auto* look = new QGroupBox(tr("Colours"));
    auto* lookForm = new QFormLayout(look);
    lookForm->setSpacing(10);
    auto* textBox = new QGroupBox(tr("Text"));
    auto* textForm = new QFormLayout(textBox);
    textForm->setSpacing(10);

    theme_ = new QComboBox;
    theme_->addItems({tr("Follow the system"), tr("Light"), tr("Dark")});
    theme_->setCurrentIndex(int(theme()));
    lookForm->addRow(tr("Theme"), theme_);

    accent_ = new QComboBox;
    for (int i = 0; i < kAccentCount; ++i) {
        accent_->addItem(tr(kAccents[i].name));
        if (i > 0) {
            // A swatch, because a colour named in words is a colour you have to
            // imagine. §14: the name carries the meaning, the swatch only helps.
            QPixmap swatch(14, 14);
            swatch.fill(QColor::fromRgba(kAccents[i].rgb));
            accent_->setItemIcon(i, QIcon(swatch));
        }
    }
    const QColor current = accent();
    accent_->setCurrentIndex(0);
    for (int i = 1; i < kAccentCount; ++i)
        if (current.isValid() && QColor::fromRgba(kAccents[i].rgb) == current)
            accent_->setCurrentIndex(i);
    lookForm->addRow(tr("Accent"), accent_);

    font_ = new QFontComboBox;
    font_->setEditable(false);
    // Napkin ships Inter so it reads the same everywhere; the first entry is
    // that, rather than a named family that might also be installed.
    font_->insertItem(0, tr("Inter (Napkin's own)"));
    const QString family = fontFamily();
    if (family.isEmpty()) font_->setCurrentIndex(0);
    else                  font_->setCurrentFont(QFont(family));
    textForm->addRow(tr("Typeface"), font_);

    scale_ = new QComboBox;
    for (int i = 0; i < kScaleCount; ++i) {
        scale_->addItem(QStringLiteral("%1%").arg(kScales[i]),
                        kScales[i]);
        if (kScales[i] == textScalePercent()) scale_->setCurrentIndex(i);
    }
    textForm->addRow(tr("Text size"), scale_);

    preview_ = new QLabel;
    preview_->setFrameShape(QFrame::StyledPanel);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setAutoFillBackground(true);   // it shows the chosen theme's surface
    preview_->setMinimumWidth(260);           // Breeze keeps form fields at their hint
    textForm->addRow(tr("Preview"), preview_);

    // Live, so the choice is made by looking rather than by guessing and
    // reopening the dialog.
    connect(font_, &QFontComboBox::currentFontChanged, this, &SettingsDialog::updatePreview);
    connect(scale_, &QComboBox::currentIndexChanged, this, &SettingsDialog::updatePreview);
    connect(accent_, &QComboBox::currentIndexChanged, this, &SettingsDialog::updatePreview);
    connect(theme_, &QComboBox::currentIndexChanged, this, &SettingsDialog::updatePreview);
    updatePreview();

    appearancePage->addWidget(look);
    appearancePage->addWidget(textBox);
    appearancePage->addStretch();

    // --- napkins and trash --------------------------------------------------
    auto* life = new QGroupBox(tr("Lifecycle"));
    // A column holding the form and, under it, the notes: QFormLayout does not
    // report a wrapped label's height upward, so notes placed in the form were
    // clipped top and bottom (usability test, 2026-09-19). A plain column does.
    auto* lifeColumn = new QVBoxLayout(life);
    auto* lifeForm = new QFormLayout;
    lifeColumn->addLayout(lifeForm);
    lifeForm->setSpacing(10);

    older_ = new QSpinBox;
    older_->setRange(1, 3650);
    older_->setSuffix(tr(" days"));
    older_->setValue(olderThanDays());
    lifeForm->addRow(tr("Move to “Older” after"), older_);

    retention_ = new QSpinBox;
    retention_->setRange(1, 3650);
    retention_->setSuffix(tr(" days"));
    retention_->setValue(trashRetentionDays());
    lifeForm->addRow(tr("Keep trash for"), retention_);

    tray_ = new QCheckBox(tr("Keep Napkin running in the system tray"));
    tray_->setChecked(QSettings().value(kKeepInTray, false).toBool());
    if (!TrayIcon::availableOnThisDesktop()) {
        tray_->setEnabled(false);
        tray_->setToolTip(tr("This desktop has no system tray."));
    }

    // Off by default: turning it on is what makes the desktop ask the user to
    // confirm a shortcut, and nobody should meet that dialog unasked.
    shortcut_ = new QCheckBox(tr("Paste into Napkin from any app with a keyboard shortcut"));
    shortcut_->setChecked(QSettings().value(kCaptureShortcut, false).toBool());
    if (!GlobalShortcut::isSupported()) {
        shortcut_->setChecked(false);
        shortcut_->setEnabled(false);
        shortcut_->setToolTip(tr("This desktop does not offer global shortcuts to applications."));
    }

    // A shortcut only works while Napkin is running, and without the tray,
    // closing the window quits it — which is how the first real-desktop test
    // of the shortcut "did nothing". Asking for the one brings the other.
    connect(shortcut_, &QCheckBox::toggled, this, [this](bool on) {
        if (on && tray_->isEnabled()) tray_->setChecked(true);
    });

    auto* trayNote = new QLabel(
        tr("Closing the window then hides Napkin instead of quitting it, so it is "
           "already running the next time you have something to put somewhere."));
    trayNote->setWordWrap(true);

    auto* note = new QLabel(
        tr("Nothing is deleted on your behalf. “Older” only changes where "
           "a napkin sits in the list; the trash is the only thing that empties, "
           "and only what you have already deleted."));
    note->setWordWrap(true);

    auto* shortcutNote = new QLabel(
        tr("Copy something anywhere, press the shortcut (Ctrl+Alt+V unless you choose "
           "another), and it lands on the napkin you have open — without leaving what "
           "you are doing. Your desktop asks you to confirm the shortcut the first "
           "time. It works while Napkin is running, so it keeps Napkin in the tray."));
    shortcutNote->setWordWrap(true);

    lifeColumn->addWidget(note);
    napkinsPage->addWidget(life);
    napkinsPage->addStretch();

    // --- in the background --------------------------------------------------
    // The tray and the global shortcut are one subject: whether Napkin stays
    // running to catch what you throw at it.
    auto* stay = new QGroupBox(tr("Staying ready"));
    auto* stayColumn = new QVBoxLayout(stay);
    stayColumn->setSpacing(6);
    stayColumn->addWidget(tray_);
    stayColumn->addWidget(trayNote);
    stayColumn->addSpacing(10);
    stayColumn->addWidget(shortcut_);
    stayColumn->addWidget(shortcutNote);
    backgroundPage->addWidget(stay);
    backgroundPage->addStretch();

    // Notes are quieter than the controls they explain.
    for (QLabel* l : {trayNote, shortcutNote, note}) {
        QPalette pal = l->palette();
        pal.setColor(l->foregroundRole(), tokens::text(pal, tokens::kTextSecondary));
        l->setPalette(pal);
    }
    nav->setCurrentRow(0);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { save(); accept(); });
}

// Shows the chosen face at the chosen size, on the chosen accent. The dialog
// itself deliberately does not restyle as you choose: a dialog that reflowed
// under the pointer would move the control you were using.
void SettingsDialog::updatePreview()
{
    const QFont sample = baseFont(font_->currentIndex() > 0 ? font_->currentFont().family()
                                                            : QString(),
                                  scale_->currentData().toInt());
    preview_->setFont(sample);
    preview_->setText(tr("The quick brown fox\n0123456789"));
    // Two explicit lines, never wrapped: it was word-wrapped inside a fixed
    // 56px box, so at larger sizes the second line was cut off.
    const QFontMetrics fm(sample);
    preview_->setMinimumHeight(fm.lineSpacing() * 2 + 24);

    // The chosen theme, in the preview only. The dialog itself still does not
    // restyle as you choose (a dialog reflowing under the pointer moves the
    // control you are using), but a preview that ignored the theme left
    // "Dark" untestable until after Save.
    const int theme = theme_->currentIndex();
    const QPalette themed = highContrast()             ? systemPalette()
                          : theme == int(Theme::Light) ? buildPalette(false, systemPalette())
                          : theme == int(Theme::Dark)  ? buildPalette(true, systemPalette())
                                                       : buildPalette(systemIsDark(), systemPalette());
    const int index = accent_->currentIndex();
    QPalette p = preview_->palette();
    p.setColor(preview_->backgroundRole(), themed.color(QPalette::Base));
    if (index > 0) {
        QPalette probe = themed;
        probe.setColor(QPalette::Highlight, QColor::fromRgba(kAccents[index].rgb));
        p.setColor(preview_->foregroundRole(), tokens::readableAccent(probe, 1.0));
    } else {
        p.setColor(preview_->foregroundRole(), tokens::text(themed, tokens::kTextPrimary));
    }
    preview_->setPalette(p);
}

void SettingsDialog::save()
{
    QSettings settings;
    settings.setValue(kTheme, theme_->currentIndex());
    settings.setValue(kFontFamily, font_->currentIndex() > 0
                                       ? font_->currentFont().family()
                                       : QString());
    settings.setValue(kTextScale, scale_->currentData().toInt());
    const int accentIndex = accent_->currentIndex();
    settings.setValue(kAccent, accentIndex > 0
                                   ? QColor::fromRgba(kAccents[accentIndex].rgb).name()
                                   : QString());
    settings.setValue(kKeepInTray, tray_->isChecked());
    settings.setValue(kCaptureShortcut, shortcut_->isChecked());
    settings.setValue(kOlder, older_->value());
    settings.setValue(kRetention, retention_->value());
    applyAppearance();
    emit settingsChanged();
}

}  // namespace napkin
