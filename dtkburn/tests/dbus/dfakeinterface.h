// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef DFAKEINTERFACE_H
#define DFAKEINTERFACE_H

#include <QObject>
#include "ddemotypes.h"

class DFakeInterface : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.freedesktop.fakelogin.Manager")
public:
    explicit DFakeInterface(QObject *parent = nullptr);
    virtual ~DFakeInterface();

    Q_PROPERTY(bool Docked READ Docked WRITE setDocked NOTIFY DockedChanged)
    bool Docked() const;
    void setDocked(bool docked = true);

public slots:
    UserPathList ListUsers();
    void setListUsers(const UserPathList &list); // addUser ?

signals:
    void DockedChanged(const bool value);
    void UserNew(uint id, const QDBusObjectPath &path);

private:
    bool registerService();
    void unRegisterService();

    UserPathList m_users;
    bool m_docked = false;
};

#endif // DFAKEINTERFACE_H
