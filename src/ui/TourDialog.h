#pragma once
#include <QDialog>
#include <QString>
#include <vector>

class QLabel;
class QPushButton;

namespace napkin {

// A short walkthrough of what Napkin can do, a step at a time.
//
// Pin, Keep, Calculate, the Trash's undo and Older are all invisible until you
// know to ask for them — a right-click menu and a key chord each — so a new
// user had no way to learn that they exist (user request, 2026-10-06). It opens
// once, on the first launch, and stays in the menu and on the start page.
//
// Every step says what is true of the app as built: each claim is a feature
// with a test behind it, not an aspiration.
class TourDialog : public QDialog {
    Q_OBJECT
public:
    explicit TourDialog(QWidget* parent = nullptr);

    // Whether it has been seen (finished or skipped) on this machine.
    static bool seen();
    static void markSeen();

    int stepCount() const { return int(steps_.size()); }
    int currentStep() const { return step_; }
    void showStep(int index);

    // What an illustration draws; one per step.
    enum class Picture { Board, Paste, PinKeep, Calculate, Search, Trash, Anywhere };

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void done(int result) override;

private:
    struct Step {
        Picture picture;
        QString title;
        QString body;   // rich text: keys in <b>
    };
    std::vector<Step> steps_;
    int step_ = 0;

    class Illustration* picture_ = nullptr;
    class Dots* dots_ = nullptr;
    QLabel* counter_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* body_ = nullptr;
    QPushButton* back_ = nullptr;
    QPushButton* next_ = nullptr;
    QPushButton* skip_ = nullptr;
};

}  // namespace napkin
