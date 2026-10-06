#pragma once
#include <QObject>
#include <QString>

namespace napkin {

// A system-wide shortcut, through the desktop's GlobalShortcuts portal
// (org.freedesktop.portal.GlobalShortcuts) — on Wayland the only way an
// application can hear a key pressed while another one has focus.
//
// The handshake waits for each Request's Response signal rather than deriving
// paths: CreateSession answers with the session handle, and BindShortcuts
// shows the desktop's own dialog, where the user accepts the shortcut or picks
// another. Nothing here reads the clipboard — Wayland serves it only to a
// focused window (§17), so the window must be raised and focused first, which
// is the caller's job.
//
// Linux only. Elsewhere isSupported() is false and nothing else does anything.
class GlobalShortcut : public QObject {
    Q_OBJECT
public:
    explicit GlobalShortcut(QObject* parent = nullptr);
    ~GlobalShortcut() override;

    // The portal is present and offers global shortcuts.
    static bool isSupported();

    // Asks for the shortcut; the first time, the desktop asks the user.
    void enable();
    void disable();
    bool isActive() const { return active_; }
    // How the desktop says the bound key, e.g. "Ctrl+Alt+V"; empty until bound.
    QString trigger() const { return trigger_; }

    // The portal's Activated signal; emits activated() for our own shortcut.
    void handleActivated(const QString& session, const QString& id);

    static constexpr const char* kShortcutId = "paste-into-napkin";
    static constexpr const char* kPreferredTrigger = "CTRL+ALT+V";

signals:
    void activated();
    // Enabled, refused or failed; read isActive() and trigger().
    void stateChanged(const QString& problem);

private:
    void createSession();
    void bind();

    QString session_;
    QString trigger_;
    bool    active_ = false;
    bool    wanted_ = false;
    QObject* listener_ = nullptr;
};

}  // namespace napkin
