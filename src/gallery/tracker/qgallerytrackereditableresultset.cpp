/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: http://www.qt-project.org/
**
** This file is part of the QtDocGallery module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qgallerytrackereditableresultset_p.h"

#include "qgallerytrackerresultset_p_p.h"
#include "qgallerytrackerschema_p.h"

#include <QtCore/qcoreapplication.h>
#include <QtDBus/qdbuspendingreply.h>

Q_DECLARE_METATYPE(QVector<QStringList>)

QT_ADDON_GALLERY_BEGIN_NAMESPACE

class QGalleryTrackerEditableResultSetPrivate : public QGalleryTrackerResultSetPrivate
{
    Q_DECLARE_PUBLIC(QGalleryTrackerEditableResultSet)
public:
    QGalleryTrackerEditableResultSetPrivate(
            QGalleryTrackerResultSetArguments *arguments,
            const QGalleryDBusInterfacePointer &metaDataInterface,
            bool autoUpdate)
        : QGalleryTrackerResultSetPrivate(arguments, autoUpdate)
        , metaDataInterface(metaDataInterface)
        , fieldNames(arguments->fieldNames)
    {
    }

    const QGalleryDBusInterfacePointer metaDataInterface;
    const QStringList fieldNames;
};

QGalleryTrackerEditableResultSet::QGalleryTrackerEditableResultSet(
        QGalleryTrackerResultSetArguments *arguments,
        const QGalleryDBusInterfacePointer &metaDataInterface,
        bool autoUpdate,
        QObject *parent)
    : QGalleryTrackerResultSet(
            *new QGalleryTrackerEditableResultSetPrivate(arguments, metaDataInterface, autoUpdate),
            parent)
{
}

QGalleryTrackerEditableResultSet::~QGalleryTrackerEditableResultSet()
{
}

bool QGalleryTrackerEditableResultSet::setMetaData(int key, const QVariant &value)
{
    Q_D(QGalleryTrackerEditableResultSet);

    if (!d->currentRow || key < d->valueOffset || key >= d->columnCount)
        return false;
    else if (key >= d->aliasOffset)
        key = d->aliasColumns.at(key - d->aliasOffset) + d->valueOffset;

    if (key >= d->compositeOffset)
        return false;

    if (*(d->currentRow + key) == value)
        return true;

    QGalleryTrackerMetaDataEdit *edit = 0;

    typedef QList<QGalleryTrackerMetaDataEdit *>::iterator iterator;
    for (iterator it = d->edits.begin(), end = d->edits.end(); it != end; ++it) {
        if ((*it)->index() == d->currentIndex) {
            edit = *it;
            break;
        }
    }

    if (!edit) {
        edit = new QGalleryTrackerMetaDataEdit(
                d->metaDataInterface,
                (d->currentRow + 1)->toString(),
                d->currentRow->toString(),
                this);
        edit->setIndex(d->currentIndex);

        connect(edit, SIGNAL(finished(QGalleryTrackerMetaDataEdit*)),
                this, SLOT(_q_editFinished(QGalleryTrackerMetaDataEdit*)));

        connect(this, SIGNAL(itemsInserted(int,int)), edit, SLOT(itemsInserted(int,int)));
        connect(this, SIGNAL(itemsRemoved(int,int)), edit, SLOT(itemsRemoved(int,int)));

        d->edits.append(edit);

        d->requestUpdate();
    }

    edit->setValue(
            d->fieldNames.at(key - d->valueOffset),
            d->valueColumns.at(key - d->valueOffset)->toString(value),
            (d->currentRow + key)->toString());

    return true;
}

QT_ADDON_GALLERY_END_NAMESPACE

#include "moc_qgallerytrackereditableresultset_p.cpp"
