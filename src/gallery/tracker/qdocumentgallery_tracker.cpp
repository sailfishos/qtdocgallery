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

#include <tracker-sparql.h>

#include "qdocumentgallery.h"

#include "qabstractgallery_p.h"

#include "qgalleryitemrequest.h"
#include "qgalleryqueryrequest.h"
#include "qgallerytyperequest.h"

#include "qgallerytrackerchangenotifier_p.h"
#include "qgallerytrackerschema_p.h"
#include "qgallerytrackereditableresultset_p.h"

#include <QtCore/qmetaobject.h>
#include <QtDBus/qdbusmetatype.h>
#include <QtDBus/qdbusargument.h>

#include <QtCore/qdebug.h>

Q_DECLARE_METATYPE(QVector<QStringList>)

QT_BEGIN_NAMESPACE_DOCGALLERY

class QDocumentGalleryPrivate : public QAbstractGalleryPrivate
{
public:
    QDocumentGalleryPrivate()
        : connection(nullptr), m_notifier(nullptr)
    {
    }

    QGalleryAbstractResponse *createItemResponse(QGalleryItemRequest *request);
    QGalleryAbstractResponse *createTypeResponse(QGalleryTypeRequest *request);
    QGalleryAbstractResponse *createFilterResponse(QGalleryQueryRequest *request);

    QGalleryAbstractResponse *createItemListResponse(
            QGalleryTrackerResultSetArguments *arguments,
            bool autoUpdate);

    TrackerSparqlConnection *connection;
    QGalleryTrackerChangeNotifier *m_notifier;
};

QGalleryAbstractResponse *QDocumentGalleryPrivate::createItemResponse(QGalleryItemRequest *request)
{
    QGalleryTrackerSchema schema = QGalleryTrackerSchema::fromItemId(request->itemId().toString());

    QGalleryTrackerResultSetArguments arguments;

    int error = schema.prepareItemResponse(
            &arguments, request->itemId().toString(), request->propertyNames());

    if (error != QDocumentGallery::NoError) {
        return new QGalleryAbstractResponse(error);
    } else {
        return createItemListResponse(
                &arguments,
                request->autoUpdate());
    }
}

QGalleryAbstractResponse *QDocumentGalleryPrivate::createTypeResponse(QGalleryTypeRequest *request)
{
    QGalleryTrackerSchema schema(request->itemType());

    QGalleryTrackerResultSetArguments arguments;

    int error = schema.prepareTypeResponse(&arguments);

    if (error != QDocumentGallery::NoError) {
        return new QGalleryAbstractResponse(error);
    } else {
        QGalleryTrackerResultSet *response = new QGalleryTrackerResultSet(connection, &arguments, request->autoUpdate());

        if (request->autoUpdate()) {
            if (m_notifier)
                QObject::connect(m_notifier, &QGalleryTrackerChangeNotifier::itemsChanged,
                                 response, &QGalleryTrackerResultSet::refresh);
        }

        return response;
    }
}

QGalleryAbstractResponse *QDocumentGalleryPrivate::createItemListResponse(
        QGalleryTrackerResultSetArguments *arguments,
        bool autoUpdate)
{
    if (!connection)
        return new QGalleryAbstractResponse(QDocumentGallery::ConnectionError);

    QGalleryTrackerResultSet *response = new QGalleryTrackerEditableResultSet(
            connection, arguments, autoUpdate);

    if (autoUpdate) {
        if (m_notifier) {
            QObject::connect(m_notifier, &QGalleryTrackerChangeNotifier::itemsChanged,
                             response, &QGalleryTrackerResultSet::refresh);
        }
    }
    if (m_notifier) {
        QObject::connect(response, &QGalleryTrackerEditableResultSet::itemEdited,
                         m_notifier, &QGalleryTrackerChangeNotifier::itemsEdited);
    }

    return response;
}

QGalleryAbstractResponse *QDocumentGalleryPrivate::createFilterResponse(
        QGalleryQueryRequest *request)
{
    QGalleryTrackerSchema schema(request->rootType());

    QGalleryTrackerResultSetArguments arguments;

    int error = schema.prepareQueryResponse(
            &arguments,
            request->scope(),
            request->rootItem().toString(),
            request->filter(),
            request->propertyNames(),
            request->sortPropertyNames(),
            request->offset(),
            request->limit());

    if (error != QDocumentGallery::NoError) {
        return new QGalleryAbstractResponse(error);
    } else {
        return createItemListResponse(
                &arguments,
                request->autoUpdate());
    }
}

QDocumentGallery::QDocumentGallery(QObject *parent)
    : QAbstractGallery(*new QDocumentGalleryPrivate, parent)
{
    Q_D(QDocumentGallery);

    qDBusRegisterMetaType<QVector<QStringList> >();

    GError *error = NULL;
    d->connection = tracker_sparql_connection_bus_new("org.freedesktop.Tracker3.Miner.Files", NULL, NULL, &error);
    if (error != NULL) {
        qWarning() << "Error creating tracker connection:" << error->message;
        g_error_free(error);
    }

    if (d->connection) {
        d->m_notifier = new QGalleryTrackerChangeNotifier(d->connection);
    }
}

QDocumentGallery::~QDocumentGallery()
{
    Q_D(QDocumentGallery);
    delete d->m_notifier;
    d->m_notifier = nullptr;

    if (d->connection)
        g_object_unref(d->connection);
}

bool QDocumentGallery::isRequestSupported(QGalleryAbstractRequest::RequestType type) const
{
    switch (type) {
    case QGalleryAbstractRequest::QueryRequest:
    case QGalleryAbstractRequest::ItemRequest:
    case QGalleryAbstractRequest::TypeRequest:
        return true;
    default:
        return false;
    }
}

QStringList QDocumentGallery::itemTypePropertyNames(const QString &itemType) const
{
    return QGalleryTrackerSchema(itemType).supportedPropertyNames();
}

QGalleryProperty::Attributes QDocumentGallery::propertyAttributes(
        const QString &propertyName, const QString &itemType) const
{
    return QGalleryTrackerSchema(itemType).propertyAttributes(propertyName);
}

QGalleryAbstractResponse *QDocumentGallery::createResponse(QGalleryAbstractRequest *request)
{
    Q_D(QDocumentGallery);

    switch (request->type()) {
    case QGalleryAbstractRequest::QueryRequest:
        return d->createFilterResponse(static_cast<QGalleryQueryRequest *>(request));
    case QGalleryAbstractRequest::ItemRequest:
        return d->createItemResponse(static_cast<QGalleryItemRequest *>(request));
    case QGalleryAbstractRequest::TypeRequest:
        return d->createTypeResponse(static_cast<QGalleryTypeRequest *>(request));
    default:
        return 0;
    }
}

QT_END_NAMESPACE_DOCGALLERY
