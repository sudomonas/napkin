#pragma once
#include <QWidget>

class QLabel;
class QPushButton;

namespace napkin {

// A dead end with a way out of it.
//
// An empty trash and a search with no hits are not "you have nothing yet" — the
// start page would be wrong there — but a bare line of text leaves you on a
// screen with nothing to do and no obvious way back. This is a picture, a
// sentence, and the one action that gets you out.
class EmptyStateView : public QWidget {
    Q_OBJECT
public:
    explicit EmptyStateView(QWidget* parent = nullptr);

    // `artwork` may be empty, in which case the space it would take is not
    // reserved. It is a resource path.
    // `actionLabel` may be empty to show no button at all.
    void setContent(const QString& artwork, const QString& title, const QString& detail,
                    const QString& actionLabel = {});

    // For tests: whether the illustration actually decoded.
    bool hasArtwork() const;

signals:
    void actionTriggered();

protected:
    void changeEvent(QEvent* e) override;

private:
    void drawArtwork();
    QString artwork_;
    void applyPalette();

    QLabel*      art_ = nullptr;
    QLabel*      title_ = nullptr;
    QLabel*      detail_ = nullptr;
    QPushButton* action_ = nullptr;
};

}  // namespace napkin
