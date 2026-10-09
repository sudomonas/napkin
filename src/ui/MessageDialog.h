#pragma once
#include <QDialog>

class QPushButton;

namespace napkin {

// Napkin's own "are you sure?" and "this went wrong", in place of QMessageBox.
//
// QMessageBox drew the platform's yellow warning triangle beside a question set
// in the same weight as its explanation, and its "Delete permanently" looked
// exactly like a safe button. Here the question is a heading, the explanation
// is quieter text under it, and an action that destroys something is red.
// Cancel stays the default, so Enter never deletes.
class MessageDialog : public QDialog {
    Q_OBJECT
public:
    enum class Kind { Confirm, Danger, Notice };

    MessageDialog(Kind kind, const QString& windowTitle, const QString& heading,
                  const QString& detail, const QString& actionLabel, QWidget* parent = nullptr);

    // True if the action was chosen. Danger paints the action red.
    static bool confirm(QWidget* parent, const QString& windowTitle, const QString& heading,
                        const QString& detail, const QString& actionLabel,
                        Kind kind = Kind::Danger);
    // One button, "OK".
    static void notify(QWidget* parent, const QString& windowTitle, const QString& heading,
                       const QString& detail);

    QString heading() const { return heading_; }
    QPushButton* actionButton() const { return action_; }
    QPushButton* cancelButton() const { return cancel_; }   // null for a notice

private:
    QString      heading_;
    QPushButton* action_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

}  // namespace napkin
