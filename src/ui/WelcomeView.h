#pragma once
#include <QWidget>

class QLabel;

namespace napkin {

// What you see when there is nothing here yet.
//
// Napkin has no menus to explore on first run and no document to open — you are
// supposed to throw something at it, and nothing on screen says so. This is the
// Neovim start-page answer: the mark, the name, what it is for in one line, and
// the handful of keys that actually matter, each of which also works as a
// button. Learning the app and using it are the same gesture.
class WelcomeView : public QWidget {
    Q_OBJECT
public:
    explicit WelcomeView(QWidget* parent = nullptr);

signals:
    void newBufferRequested();
    void pasteRequested();
    void newTextRequested();
    void addImageRequested();
    void searchRequested();
    void tourRequested();   // "Take the tour"
    // Someone started typing on the start page, which says "dump text here":
    // the text should become a note in a new napkin, not vanish.
    void textTyped(const QString& text);

protected:
    void changeEvent(QEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    QWidget* buildShortcutRow(const QString& keys, const QString& what, const char* slot);
    void applyPalette();

    QLabel* logo_ = nullptr;
    QLabel* name_ = nullptr;
    QLabel* tagline_ = nullptr;
    QLabel* instruction_ = nullptr;
    QLabel* footer_ = nullptr;
};

}  // namespace napkin
