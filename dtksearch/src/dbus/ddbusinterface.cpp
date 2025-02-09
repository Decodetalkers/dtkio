// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "ddbusinterface.h"
#include "ddbusinterface_p.h"

#include <QMetaObject>
#include <qmetaobject.h>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusPendingReply>
#include <QDebug>

static const QString &FreedesktopService = QStringLiteral("org.freedesktop.DBus");
static const QString &FreedesktopPath = QStringLiteral("/org/freedesktop/DBus");
static const QString &FreedesktopInterface = QStringLiteral("org.freedesktop.DBus");
static const QString &NameOwnerChanged = QStringLiteral("NameOwnerChanged");

static const QString &PropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
static const QString &PropertiesChanged = QStringLiteral("PropertiesChanged");
static const char *PropertyName = "propname";

DDBusInterfacePrivate::DDBusInterfacePrivate(DDBusInterface *interface, QObject *parent)
    : QObject(interface)
    , m_parent(parent)
    , m_serviceValid(false)
    , q_ptr(interface)
{
    QDBusMessage message = QDBusMessage::createMethodCall(FreedesktopService, FreedesktopPath, FreedesktopInterface, "NameHasOwner");
    message << interface->service();
    interface->connection().callWithCallback(message, this, SLOT(onDBusNameHasOwner(bool)));

    interface->connection().connect(interface->service(),
                                    interface->path(),
                                    PropertiesInterface,
                                    PropertiesChanged,
                                    {interface->interface()},
                                    QString(),
                                    this,
                                    SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
}

void DDBusInterfacePrivate::updateProp(const char *propName, const QVariant &value)
{
    if (!m_parent)
        return;
    m_propertyMap.insert(propName, value);
    const QMetaObject *metaObj = m_parent->metaObject();
    QByteArray baSignal = QStringLiteral("%1Changed(%2)").arg(propName).arg(value.typeName()).toLatin1();
    QByteArray baSignalName = QStringLiteral("%1Changed").arg(propName).toLatin1();
    const char *signal = baSignal.data();
    const char *signalName = baSignalName.data();
    int i = metaObj->indexOfSignal(signal);
    if (i != -1) {
        QMetaObject::invokeMethod(m_parent, signalName, Qt::DirectConnection, QGenericArgument(value.typeName(), value.data()));
    } else
        qWarning() << "invalid property changed:" << propName << value;
}

void DDBusInterfacePrivate::initDBusConnection()
{
    if (!m_parent)
        return;
    Q_Q(DDBusInterface);
    QDBusConnection connection = q->connection();
    QStringList signalList;
    QDBusInterface inter(q->service(), q->path(), q->interface(), connection);
    const QMetaObject *meta = inter.metaObject();
    for (int i = meta->methodOffset(); i < meta->methodCount(); ++i) {
        const QMetaMethod &method = meta->method(i);
        if (method.methodType() == QMetaMethod::Signal) {
            signalList << method.methodSignature();
        }
    }
    const QMetaObject *parentMeta = m_parent->metaObject();
    for (const QString &signal : signalList) {
        int i = parentMeta->indexOfSignal(QMetaObject::normalizedSignature(signal.toLatin1()));
        if (i != -1) {
            const QMetaMethod &parentMethod = parentMeta->method(i);
            connection.connect(q->service(),
                               q->path(),
                               q->interface(),
                               parentMethod.name(),
                               m_parent,
                               QT_STRINGIFY(QSIGNAL_CODE) + parentMethod.methodSignature());
        }
    }
}

void DDBusInterfacePrivate::onPropertiesChanged(const QString &interfaceName,
                                                const QVariantMap &changedProperties,
                                                const QStringList &invalidatedProperties)
{
    Q_UNUSED(interfaceName)
    Q_UNUSED(invalidatedProperties)
    for (QVariantMap::const_iterator it = changedProperties.cbegin(); it != changedProperties.cend(); ++it)
        updateProp((it.key() + m_suffix).toLatin1(), it.value());
}

void DDBusInterfacePrivate::onAsyncPropertyFinished(QDBusPendingCallWatcher *w)
{
    QDBusPendingReply<QVariant> reply = *w;
    if (!reply.isError()) {
        updateProp(w->property(PropertyName).toString().toLatin1(), reply.value());
    }
    w->deleteLater();
}

void DDBusInterfacePrivate::setServiceValid(bool valid)
{
    if (m_serviceValid != valid) {
        Q_Q(DDBusInterface);
        m_serviceValid = valid;
        Q_EMIT q->serviceValidChanged(m_serviceValid);
    }
}

void DDBusInterfacePrivate::onDBusNameHasOwner(bool valid)
{
    Q_Q(DDBusInterface);
    setServiceValid(valid);
    if (valid)
        initDBusConnection();
    else
        q->connection().connect(FreedesktopService,
                                FreedesktopPath,
                                FreedesktopInterface,
                                NameOwnerChanged,
                                this,
                                SLOT(onDBusNameOwnerChanged(QString, QString, QString)));
}

void DDBusInterfacePrivate::onDBusNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner)
{
    Q_Q(DDBusInterface);
    if (name == q->service() && oldOwner.isEmpty()) {
        initDBusConnection();
        q->connection().disconnect(FreedesktopService,
                                   FreedesktopPath,
                                   FreedesktopInterface,
                                   NameOwnerChanged,
                                   this,
                                   SLOT(onDBusNameOwnerChanged(QString, QString, QString)));
        setServiceValid(true);
    } else if (name == q->service() && newOwner.isEmpty())
        setServiceValid(false);
}
//////////////////////////////////////////////////////////
DDBusInterface::DDBusInterface(const QString &service, const QString &path, const QString &interface, const QDBusConnection &connection, QObject *parent)
    : QDBusAbstractInterface(service, path, interface.toLatin1(), connection, parent)
    , d_ptr(new DDBusInterfacePrivate(this, parent))
{
}

DDBusInterface::~DDBusInterface() {}

bool DDBusInterface::serviceValid() const
{
    Q_D(const DDBusInterface);
    return d->m_serviceValid;
}

QString DDBusInterface::suffix() const
{
    Q_D(const DDBusInterface);
    return d->m_suffix;
}

void DDBusInterface::setSuffix(const QString &suffix)
{
    Q_D(DDBusInterface);
    d->m_suffix = suffix;
}

inline QString originalPropname(const char *propname, QString suffix)
{
    QString propStr(propname);
    return propStr.left(propStr.length() - suffix.length());
}

QVariant DDBusInterface::property(const char *propName)
{
    Q_D(DDBusInterface);
    if (d->m_propertyMap.contains(propName))
        return d->m_propertyMap.value(propName);

    QDBusMessage msg = QDBusMessage::createMethodCall(service(), path(), PropertiesInterface, QStringLiteral("Get"));
    msg << interface() << originalPropname(propName, d->m_suffix);
    QDBusPendingReply<QVariant> prop = connection().asyncCall(msg);
    if (prop.value().isValid())
        return prop.value();

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(prop, this);
    watcher->setProperty(PropertyName, propName);
    connect(watcher, &QDBusPendingCallWatcher::finished, d, &DDBusInterfacePrivate::onAsyncPropertyFinished);
    if (d->m_propertyMap.contains(propName))
        return d->m_propertyMap.value(propName);

    return QVariant();
}

void DDBusInterface::setProperty(const char *propName, const QVariant &value)
{
    Q_D(const DDBusInterface);
    QDBusMessage msg = QDBusMessage::createMethodCall(service(), path(), PropertiesInterface, QStringLiteral("Set"));
    msg << interface() << originalPropname(propName, d->m_suffix) << QVariant::fromValue(QDBusVariant(value));

    QDBusPendingReply<void> reply = connection().asyncCall(msg);
    reply.waitForFinished();
    if (!reply.isValid()) {
        qWarning() << reply.error().message();
    }
}
