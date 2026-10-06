#include "GlobalShortcut.h"

#ifdef NAPKIN_HAVE_PORTAL
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QRandomGenerator>
#include <functional>

namespace {

const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kInterface = QStringLiteral("org.freedesktop.portal.GlobalShortcuts");

// a(sa{sv}): what BindShortcuts takes and its Response returns.
struct PortalShortcut {
    QString id;
    QVariantMap properties;
};
using PortalShortcuts = QList<PortalShortcut>;

QDBusArgument& operator<<(QDBusArgument& arg, const PortalShortcut& s)
{
    arg.beginStructure();
    arg << s.id << s.properties;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, PortalShortcut& s)
{
    arg.beginStructure();
    arg >> s.id >> s.properties;
    arg.endStructure();
    return arg;
}

}  // namespace

Q_DECLARE_METATYPE(PortalShortcut)
Q_DECLARE_METATYPE(PortalShortcuts)

namespace napkin {
namespace {

// One portal Request: subscribed to before the call is made, so a fast
// Response cannot be missed, and deleted once it has answered.
class PortalRequest : public QObject {
    Q_OBJECT
public:
    using Done = std::function<void(uint code, const QVariantMap& results)>;

    static void call(QObject* owner, const QString& method, QList<QVariant> args,
                     QVariantMap options, Done done)
    {
        auto bus = QDBusConnection::sessionBus();
        const QString token = QStringLiteral("napkin%1").arg(QRandomGenerator::global()->generate());
        options.insert(QStringLiteral("handle_token"), token);
        // The documented form of the request path: the caller's unique name
        // without the colon, dots as underscores, then the token.
        QString sender = bus.baseService().mid(1);
        sender.replace(QLatin1Char('.'), QLatin1Char('_'));
        const QString path = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
                                 .arg(sender, token);

        auto* request = new PortalRequest(owner, std::move(done));
        bus.connect(kService, path, QStringLiteral("org.freedesktop.portal.Request"),
                    QStringLiteral("Response"), request,
                    SLOT(onResponse(uint, QVariantMap)));

        QDBusMessage message = QDBusMessage::createMethodCall(kService, kPath, kInterface, method);
        args << options;
        message.setArguments(args);
        const QDBusMessage reply = bus.call(message);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            request->finish(2, {{QStringLiteral("error"), reply.errorMessage()}});
            return;
        }
        // The reply names the request actually made. If the portal chose a
        // different path, follow that one instead.
        const auto actual = reply.arguments().value(0).value<QDBusObjectPath>().path();
        if (!actual.isEmpty() && actual != path) {
            bus.disconnect(kService, path, QStringLiteral("org.freedesktop.portal.Request"),
                           QStringLiteral("Response"), request,
                           SLOT(onResponse(uint, QVariantMap)));
            bus.connect(kService, actual, QStringLiteral("org.freedesktop.portal.Request"),
                        QStringLiteral("Response"), request,
                        SLOT(onResponse(uint, QVariantMap)));
        }
    }

private slots:
    void onResponse(uint code, const QVariantMap& results) { finish(code, results); }

private:
    PortalRequest(QObject* owner, Done done) : QObject(owner), done_(std::move(done)) {}
    void finish(uint code, const QVariantMap& results)
    {
        if (done_) { auto d = std::move(done_); d(code, results); }
        deleteLater();
    }
    Done done_;
};

// The Activated signal carries an object path, so the slot must take one or
// QtDBus never delivers it — silently. Kept out of the header so the header
// needs no D-Bus types on platforms without them.
class ActivationListener : public QObject {
    Q_OBJECT
public:
    explicit ActivationListener(GlobalShortcut* owner) : QObject(owner), owner_(owner) {}
public slots:
    void onActivated(const QDBusObjectPath& session, const QString& id)
    {
        owner_->handleActivated(session.path(), id);
    }
private:
    GlobalShortcut* owner_;
};

}  // namespace

GlobalShortcut::GlobalShortcut(QObject* parent) : QObject(parent)
{
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<PortalShortcuts>();
}

GlobalShortcut::~GlobalShortcut() { disable(); }

bool GlobalShortcut::isSupported()
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return false;
    QDBusMessage get = QDBusMessage::createMethodCall(
        kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    get << kInterface << QStringLiteral("version");
    const QDBusMessage reply = bus.call(get, QDBus::Block, 1000);
    return reply.type() == QDBusMessage::ReplyMessage
        && reply.arguments().value(0).value<QDBusVariant>().variant().toUInt() >= 1;
}

void GlobalShortcut::enable()
{
    wanted_ = true;
    if (active_ || !session_.isEmpty()) return;

    // A program not run from a sandbox has no app id the portal can see. KDE
    // files the shortcut under it, and without one the desktop cannot say
    // whose shortcut it is. Register must come before any other portal call
    // on this connection; a second Register fails harmlessly.
    auto bus = QDBusConnection::sessionBus();
    QDBusMessage reg = QDBusMessage::createMethodCall(
        kService, kPath, QStringLiteral("org.freedesktop.host.portal.Registry"),
        QStringLiteral("Register"));
    reg << QStringLiteral("io.github.sudomonas.Napkin") << QVariantMap{};
    bus.call(reg, QDBus::Block, 2000);

    if (!listener_) {
        listener_ = new ActivationListener(this);
        bus.connect(kService, kPath, kInterface, QStringLiteral("Activated"), listener_,
                    SLOT(onActivated(QDBusObjectPath, QString)));
    }
    createSession();
}

void GlobalShortcut::createSession()
{
    const QString sessionToken =
        QStringLiteral("napkinsession%1").arg(QRandomGenerator::global()->generate());
    PortalRequest::call(this, QStringLiteral("CreateSession"), {},
                        {{QStringLiteral("session_handle_token"), sessionToken}},
                        [this](uint code, const QVariantMap& results) {
        if (code != 0) {
            emit stateChanged(tr("The desktop did not allow a global shortcut."));
            return;
        }
        session_ = results.value(QStringLiteral("session_handle")).toString();
        if (!wanted_) { disable(); return; }
        bind();
    });
}

void GlobalShortcut::bind()
{
    PortalShortcuts shortcuts{{QString::fromLatin1(kShortcutId),
                               {{QStringLiteral("description"), tr("Paste into Napkin")},
                                {QStringLiteral("preferred_trigger"),
                                 QString::fromLatin1(kPreferredTrigger)}}}};
    PortalRequest::call(this, QStringLiteral("BindShortcuts"),
                        {QVariant::fromValue(QDBusObjectPath(session_)),
                         QVariant::fromValue(shortcuts), QString()},
                        {}, [this](uint code, const QVariantMap& results) {
        if (code != 0) {
            // 1 is the user saying no in the desktop's dialog. Not an error,
            // but the shortcut is not there, and the setting must say so.
            active_ = false;
            emit stateChanged(code == 1 ? tr("The shortcut was not accepted.")
                                        : tr("The desktop could not set the shortcut."));
            return;
        }
        trigger_.clear();
        const auto bound = qdbus_cast<PortalShortcuts>(
            results.value(QStringLiteral("shortcuts")).value<QDBusArgument>());
        for (const auto& s : bound)
            if (s.id == QLatin1String(kShortcutId))
                trigger_ = s.properties.value(QStringLiteral("trigger_description")).toString();
        active_ = true;
        emit stateChanged({});
    });
}

void GlobalShortcut::disable()
{
    wanted_ = false;
    if (!session_.isEmpty()) {
        QDBusMessage close = QDBusMessage::createMethodCall(
            kService, session_, QStringLiteral("org.freedesktop.portal.Session"),
            QStringLiteral("Close"));
        QDBusConnection::sessionBus().call(close, QDBus::NoBlock);
        session_.clear();
    }
    if (active_) { active_ = false; emit stateChanged({}); }
}

void GlobalShortcut::handleActivated(const QString& session, const QString& id)
{
    if (active_ && session == session_ && id == QLatin1String(kShortcutId)) emit activated();
}

}  // namespace napkin

#include "GlobalShortcut.moc"

#else   // no portal on this platform

namespace napkin {
GlobalShortcut::GlobalShortcut(QObject* parent) : QObject(parent) {}
GlobalShortcut::~GlobalShortcut() = default;
bool GlobalShortcut::isSupported() { return false; }
void GlobalShortcut::enable() {}
void GlobalShortcut::disable() {}
void GlobalShortcut::handleActivated(const QString&, const QString&) {}
void GlobalShortcut::createSession() {}
void GlobalShortcut::bind() {}
}  // namespace napkin

#endif
