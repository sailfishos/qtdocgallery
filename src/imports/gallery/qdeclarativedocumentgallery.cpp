/****************************************************************************
**
** Copyright (C) 2012 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtDocGallery module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qdeclarativedocumentgallery.h"

#include <qgalleryabstractrequest.h>

#include <QtCore/qmetaobject.h>
#include <QtQml/qqmlcontext.h>
#include <QtQml/qqmlengine.h>
#include <QCoreApplication>
#include <QDebug>

QT_BEGIN_NAMESPACE_DOCGALLERY

QString QDeclarativeDocumentGallery::toString(ItemType type)
{
    return type != InvalidType
            ? QString::fromLatin1(staticMetaObject.enumerator(0).valueToKey(type))
            : QString();
}

QDeclarativeDocumentGallery::ItemType QDeclarativeDocumentGallery::itemTypeFromString(
        const QString &string)
{
    const int key = staticMetaObject.enumerator(0).keyToValue(string.toLatin1().constData());

    return key != -1
            ? ItemType(key)
            : InvalidType;
}

QAbstractGallery *QDeclarativeDocumentGallery::gallery(QObject *object)
{
#ifndef QTM_BUILD_UNITTESTS
    Q_UNUSED(object);
#else
    if (QQmlContext *context = QQmlEngine::contextForObject(object)) {
        if (QAbstractGallery *gallery = qobject_cast<QAbstractGallery *>(
                context->contextProperty(QLatin1String("qt_testGallery")).value<QObject *>())) {
            return gallery;
        }
    }
#endif
    static QDocumentGallery* instance = nullptr;

    if (!instance) {
        QCoreApplication *app = QCoreApplication::instance();
        if (!app) {
            qWarning() << "No QCoreApplication, refusing to create QDocumentGallery instance for QML";
            return nullptr;
        }

        instance = new QDocumentGallery(app);
    }

    return instance;
}

QT_END_NAMESPACE_DOCGALLERY

#include "moc_qdeclarativedocumentgallery.cpp"
