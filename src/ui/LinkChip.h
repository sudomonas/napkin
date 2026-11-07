#pragma once
#include <QWidget>

class QLabel;
class QPushButton;

namespace napkin {

// What a text item renders as when its whole content is one URL (SPEC.md §3).
//
// Derived presentation, not a stored type: the item is still text, still
// editable, still in the text index. This is only how it is drawn.
//
// The chip names the host QUrl resolves, never the raw string. A URL written
// "https://bank.example@evil.example/" reads as bank.example to anyone skimming
// it, and a chip that repeated that would be lending the deception its own
// credibility.
class LinkChip : public QWidget {
    Q_OBJECT
public:
    explicit LinkChip(QWidget* parent = nullptr);

    void setUrl(const QString& url);
    QString url() const { return url_; }

    // At the font the chip will actually be drawn in — the card's, which on a
    // zoomed board is not the application's. Measuring against the application
    // font sized a 150% chip for 100% and cut its label off at the top.
    static int preferredHeight(const QFont& base);

signals:
    void openRequested(const QString& url);

protected:
    void changeEvent(QEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void applyPalette();

    void elidePath();

    QString      url_;
    QString      path_;   // full, before elision to the width available
    QString      hostText_;   // likewise for the host
    QLabel*      host_ = nullptr;
    QLabel*      rest_ = nullptr;
    QPushButton* open_ = nullptr;
};

}  // namespace napkin
