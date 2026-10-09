#include "MessageDialog.h"
#include "NapkinStyle.h"
#include "Tokens.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace napkin {

MessageDialog::MessageDialog(Kind kind, const QString& windowTitle, const QString& heading,
                             const QString& detail, const QString& actionLabel, QWidget* parent)
    : QDialog(parent), heading_(heading)
{
    setWindowTitle(windowTitle);
    setAccessibleName(heading);
    setAccessibleDescription(detail);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(tokens::kHeaderGap - 8, tokens::kHeaderGap - 8,
                               tokens::kHeaderGap - 8, tokens::kWindowMargin + 4);
    column->setSpacing(tokens::kGapTight);
    // Wide enough that a two-line explanation stays two lines; a word-wrapped
    // label otherwise asks for the narrowest width it can survive at.
    column->setSizeConstraint(QLayout::SetFixedSize);

    auto* title = new QLabel(heading);
    title->setObjectName(QStringLiteral("dialogHeading"));
    title->setWordWrap(true);
    title->setFixedWidth(380);
    title->setFont(tokens::scaledBy(title->font(), 1.15, QFont::DemiBold));
    column->addWidget(title);

    if (!detail.isEmpty()) {
        auto* body = new QLabel(detail);
        body->setObjectName(QStringLiteral("dialogDetail"));
        body->setWordWrap(true);
        body->setFixedWidth(380);
        QPalette pal = body->palette();
        pal.setColor(body->foregroundRole(), tokens::text(pal, tokens::kTextSecondary));
        body->setPalette(pal);
        column->addWidget(body);
    }

    column->addSpacing(tokens::kWindowMargin);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(tokens::kGapTight);
    buttons->addStretch();
    column->addLayout(buttons);

    action_ = new QPushButton(actionLabel);
    action_->setObjectName(QStringLiteral("dialogAction"));
    connect(action_, &QPushButton::clicked, this, &QDialog::accept);

    if (kind == Kind::Notice) {
        buttons->addWidget(action_);
        action_->setDefault(true);
        action_->setFocus();
        return;
    }

    cancel_ = new QPushButton(tr("Cancel"));
    cancel_->setObjectName(QStringLiteral("dialogCancel"));
    connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel_);
    buttons->addWidget(action_);

    if (kind == Kind::Danger) {
        // Enter keeps things: Cancel is the default. It is painted plain, not
        // in the accent, so the red action is the one button that stands out.
        action_->setProperty(NapkinStyle::kShapeProperty, QStringLiteral("danger"));
        cancel_->setProperty(NapkinStyle::kQuietDefaultProperty, true);
        cancel_->setDefault(true);
        cancel_->setFocus();
    } else {
        action_->setDefault(true);
        action_->setFocus();
    }
}

bool MessageDialog::confirm(QWidget* parent, const QString& windowTitle, const QString& heading,
                            const QString& detail, const QString& actionLabel, Kind kind)
{
    MessageDialog dialog(kind, windowTitle, heading, detail, actionLabel, parent);
    return dialog.exec() == QDialog::Accepted;
}

void MessageDialog::notify(QWidget* parent, const QString& windowTitle, const QString& heading,
                           const QString& detail)
{
    MessageDialog dialog(Kind::Notice, windowTitle, heading, detail, tr("OK"), parent);
    dialog.exec();
}

}  // namespace napkin
