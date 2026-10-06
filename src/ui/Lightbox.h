#pragma once
#include <QDialog>
#include <QPixmap>
#include <vector>

class QLabel;
class QMovie;

namespace napkin {

// Full-size image view, and where an animation actually plays. The answer to
// the one real problem the master-detail mockup exposed (§7): a 2560x1440
// screenshot is cramped in a card, but a permanent second pane makes every
// text buffer pay for that.
class Lightbox : public QDialog {
    Q_OBJECT
public:
    struct Image {
        QString path;
        bool    animated = false;
        QString caption;
    };

    // Every image on the napkin, in board order, opened at `start`. ←/→ page
    // through them; each one used to have to be closed and the next opened
    // from its card, which made the lightbox the one place arrow keys did
    // not do the obvious thing.
    Lightbox(std::vector<Image> images, int start, QWidget* parent = nullptr);
    Lightbox(const QString& imagePath, bool animated, QString caption, QWidget* parent = nullptr);

    int currentIndex() const { return index_; }
    int count() const { return int(images_.size()); }
    // Moves to another image; false at either end, where it stays put.
    bool showImage(int index);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    QSize load(const Image& image);
    void rescale();

    std::vector<Image> images_;
    int     index_ = -1;
    QPixmap source_;
    QMovie* movie_ = nullptr;
    QLabel* view_  = nullptr;
};

}  // namespace napkin
