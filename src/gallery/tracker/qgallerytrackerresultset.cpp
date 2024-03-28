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

#include "qgallerytrackerresultset_p_p.h"

#include "qgallerytrackermetadataedit_p.h"
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QTimer>
#else
#include <QtCore/qdatetime.h>
#endif
#include <QtDBus/qdbusreply.h>

#include <qdocumentgallery.h>
#include <qgalleryresource.h>

QT_BEGIN_NAMESPACE_DOCGALLERY

void QGalleryTrackerResultSetPrivate::update()
{
    flags &= ~UpdateRequested;

    updateTimer.stop();

    typedef QList<QGalleryTrackerMetaDataEdit *>::iterator iterator;
    for (iterator it = edits.begin(), end = edits.end(); it != end; ++it)
        (*it)->commit();
    edits.clear();

    if (!(flags & (Active | Cancelled))) {
        query();

        flags &= ~Refresh;
    }
}

void QGalleryTrackerResultSetPrivate::query()
{
    flags &= ~(Refresh | SyncFinished);
    flags |= Active;

    updateTimer.stop();

    rCache.count = iCache.count;
    rCache.offset = 0;

    iCache.count = 0;
    iCache.cutoff = 0;

    qSwap(rCache.values, iCache.values);

    parserThread.start(QThread::LowPriority);

    Q_EMIT q_func()->progressChanged(progressMaximum - 1, progressMaximum);
}

void QGalleryTrackerResultSetPrivate::run()
{
    iCache.values.clear();

    GError *error = 0;
    if (TrackerSparqlCursor *cursor = tracker_sparql_connection_query(
                connection, sparql.toUtf8(), 0, &error)) {
        const QVariant variant;
        while (tracker_sparql_cursor_next(cursor, 0, 0)) {
            const int rowWidth = qMin(tableWidth, tracker_sparql_cursor_get_n_columns(cursor));
            int i = 0;
            for (; i < rowWidth; ++i) {
                iCache.values.append(valueColumns.at(i)->toVariant(cursor, i));
            }
            for (; i < tableWidth; ++i)
                iCache.values.append(variant);
        }
        g_object_unref(G_OBJECT(cursor));
    } else {
        queryError = QDocumentGallery::FilterError;
        queryErrorString = QString::fromUtf8(error->message);
        g_error_free(error);
    }

    iCache.count = iCache.values.count() / tableWidth;

    synchronize();
}

void QGalleryTrackerResultSetPrivate::synchronize()
{
    const const_row_iterator rEnd(rCache.values.constEnd(), tableWidth);
    const const_row_iterator iEnd(iCache.values.constEnd(), tableWidth);

    const_row_iterator rBegin(rCache.values.constBegin(), tableWidth);
    const_row_iterator iBegin(iCache.values.constBegin(), tableWidth);

    const int rStep = qMax(64, rEnd - rBegin) / 16;
    const int iStep = qMax(64, iEnd - iBegin) / 16;

    for (bool equal = true; equal && rBegin != rEnd && iBegin != iEnd; ) {
        bool changed = false;

        do {    // Skip over identical rows.
            if ((equal = rBegin.isEqual(iBegin, identityWidth))
                    && !(changed = !rBegin.isEqual(iBegin, identityWidth, tableWidth))) {
                ++rBegin;
                ++iBegin;
            } else {
                break;
            }
        } while (rBegin != rEnd && iBegin != iEnd);

        if (changed) {
            const_row_iterator rIt = rBegin;
            const_row_iterator iIt = iBegin;

            do {    // Skip over rows with equal IDs but different values.
                if ((equal = rIt.isEqual(iIt, identityWidth))
                        && rIt.isEqual(iIt, identityWidth, tableWidth)) {
                    ++rIt;
                    ++iIt;
                } else {
                    ++rIt;
                    ++iIt;

                    break;
                }
            } while (rIt != rEnd && iIt != iEnd);

            const int rIndex = rCacheIndex(rBegin);
            const int iIndex = iCacheIndex(iBegin);
            const int count = iIt - iBegin;

            postSyncEvent(SyncEvent::updateEvent(rIndex, iIndex, count));

            rBegin = rIt;
            iBegin = iIt;

            continue;
        } else if (equal) {
            postSyncEvent(SyncEvent::finishEvent(rCacheIndex(rBegin), iCacheIndex(iBegin)));

            return;
        }

        const_row_iterator rOuterEnd = rBegin + ((((rEnd - rBegin) + iStep - 1) / rStep) * rStep);
        const_row_iterator iOuterEnd = iBegin + ((((iEnd - iBegin) + iStep - 1) / iStep) * iStep);

        const_row_iterator rInnerEnd = qMin(rBegin + rStep * 2, rEnd);
        const_row_iterator iInnerEnd = qMin(iBegin + iStep * 2, iEnd);

        for (const_row_iterator rOuter = rBegin, iOuter = iBegin;
                !equal && rOuter != rOuterEnd && iOuter != iOuterEnd;
                rOuter += rStep, iOuter += iStep) {
            for (const_row_iterator rInner = rBegin, iInner = iBegin;
                    rInner != rInnerEnd && iInner != iInnerEnd;
                    ++rInner, ++iInner) {
                if ((equal = rInner.isEqual(iOuter, identityWidth))) {
                    const_row_iterator rIt;
                    const_row_iterator iIt;

                    do {
                        rIt = rInner;
                        iIt = iOuter;
                    } while (rInner-- != rBegin && iOuter-- != iBegin
                             && rInner.isEqual(iOuter, identityWidth));

                    const int rIndex = rCacheIndex(rOuter);
                    const int iIndex = iCacheIndex(iBegin);
                    const int rCount = rIt - rBegin;
                    const int iCount = iIt - iBegin;

                    postSyncEvent(SyncEvent::replaceEvent(rIndex, rCount, iIndex, iCount));

                    rBegin = rIt;
                    iBegin = iIt;

                    break;
                } else if ((equal = iInner.isEqual(rOuter, identityWidth))) {
                    const_row_iterator rIt;
                    const_row_iterator iIt;

                    do {
                        rIt = rOuter;
                        iIt = iInner;
                    } while (iInner-- != iBegin && rOuter-- != rBegin
                           && iInner.isEqual(rOuter, identityWidth));

                    const int rIndex = rCacheIndex(rBegin);
                    const int iIndex = iCacheIndex(iOuter);
                    const int rCount = rIt - rBegin;
                    const int iCount = iIt - iBegin;

                    postSyncEvent(SyncEvent::replaceEvent(rIndex, rCount, iIndex, iCount));

                    rBegin = rIt;
                    iBegin = iIt;

                    break;
                }
            }
        }
    }

    postSyncEvent(SyncEvent::finishEvent(rCacheIndex(rBegin), iCacheIndex(iBegin)));
}

void QGalleryTrackerResultSetPrivate::processSyncEvents()
{
    while (SyncEvent *event = syncEvents.dequeue()) {
        switch (event->type) {
        case SyncEvent::Update:
            syncUpdate(event->rIndex, event->rCount, event->iIndex, event->iCount);
            break;
        case SyncEvent::Replace:
            syncReplace(event->rIndex, event->rCount, event->iIndex, event->iCount);
            break;
        case SyncEvent::Finish:
            syncFinish(event->rIndex, event->iIndex);
            break;
        default:
            break;
        }

        delete event;
    }
}

void QGalleryTrackerResultSetPrivate::removeItems(
        const int rIndex, const int iIndex, const int count)
{
    const int originalIndex = currentIndex;

    rCache.offset = rIndex + count;
    iCache.cutoff = iIndex;

    if (currentIndex >= iIndex && currentIndex < rCache.offset) {
        currentIndex = iIndex;

        if (currentIndex < rCache.count) {
            currentRow = rCache.values.constBegin()
                    + ((currentIndex + rCache.offset - iCache.cutoff) * tableWidth);
        } else {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            currentRow = rCache.values.begin();
#else
            currentRow = 0;
#endif
        }
    }

    rowCount -= count;

    Q_EMIT q_func()->itemsRemoved(iIndex, count);

    if (originalIndex != currentIndex) {
        Q_EMIT q_func()->currentIndexChanged(currentIndex);
        Q_EMIT q_func()->currentItemChanged();
    }
}

void QGalleryTrackerResultSetPrivate::insertItems(
        const int rIndex, const int iIndex, const int count)
{
    rCache.offset = rIndex;
    iCache.cutoff = iIndex + count;

    rowCount += count;

    Q_EMIT q_func()->itemsInserted(iIndex, count);
}

void QGalleryTrackerResultSetPrivate::syncUpdate(
        const int rIndex, const int rCount, const int iIndex, const int iCount)
{
    bool itemChanged = false;

    if (currentIndex >= iCache.cutoff && currentIndex < iIndex + iCount) {
        currentRow = iCache.values.constBegin() + (currentIndex * tableWidth);

        itemChanged = true;
    }
    rCache.offset = rIndex + rCount;
    iCache.cutoff = iIndex + iCount;

    Q_EMIT q_func()->metaDataChanged(iIndex, iCount, propertyKeys);

    if (itemChanged)
        Q_EMIT q_func()->currentItemChanged();
}

void QGalleryTrackerResultSetPrivate::syncReplace(
        const int rIndex, const int rCount, const int iIndex, const int iCount)
{
    bool itemChanged = false;

    if (rCount > 0)
        removeItems(rIndex, iIndex, rCount);

    if (currentIndex >= iCache.cutoff && currentIndex < iIndex + iCount) {
        currentRow = iCache.values.constBegin() + (currentIndex * tableWidth);

        itemChanged = true;
    }

    if (iCount > 0)
        insertItems(rIndex + rCount, iIndex, iCount);

    if (itemChanged)
        Q_EMIT q_func()->currentItemChanged();
}

void QGalleryTrackerResultSetPrivate::syncFinish(const int rIndex, const int iIndex)
{
    const int rCount = rCache.count - rIndex;
    const int iCount = iCache.count - iIndex;

    bool itemChanged = false;

    if (rCount > 0)
        removeItems(rIndex, iIndex, rCount);
    else
        rCache.offset = rCache.count;

    if (currentIndex >= iCache.cutoff && currentIndex < iCache.count) {
        currentRow = iCache.values.constBegin() + (currentIndex * tableWidth);

        itemChanged = true;
    }

    if (iCount > 0)
        insertItems(rIndex + rCount, iIndex, iCount);
    else
        iCache.cutoff = iCache.count;

    if (itemChanged)
        Q_EMIT q_func()->currentItemChanged();

    flags |= SyncFinished;
}

bool QGalleryTrackerResultSetPrivate::waitForSyncFinish(int msecs)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QTimer timer;
#else
    QTime timer;
#endif
    timer.start();

    do {
        processSyncEvents();

        if (flags & SyncFinished) {
            return true;
        }

        if (!syncEvents.waitForEvent(msecs))
            return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    } while ((msecs -= timer.remainingTime()) > 0);
#else
    } while ((msecs -= timer.restart()) > 0);
#endif
    return false;
}

void QGalleryTrackerResultSetPrivate::_q_parseFinished()
{
    processSyncEvents();

    Q_ASSERT(rCache.offset == rCache.count);
    Q_ASSERT(iCache.cutoff == iCache.count);

    rCache.values.clear();
    rCache.count = 0;

    flags &= ~Active;

    if (flags & Refresh)
        update();
    else
        Q_EMIT q_func()->progressChanged(progressMaximum, progressMaximum);

    if (queryError != QDocumentGallery::NoError) {
        q_func()->finish(flags & Live);
    } else  {
        q_func()->error(queryError, queryErrorString);
        queryError = QDocumentGallery::NoError;
    }
}

void QGalleryTrackerResultSetPrivate::_q_editFinished(QGalleryTrackerMetaDataEdit *edit)
{
    edit->deleteLater();

    Q_EMIT q_func()->itemEdited(m_service);
}

QGalleryTrackerResultSet::QGalleryTrackerResultSet(
        TrackerSparqlConnection *connection,
        QGalleryTrackerResultSetArguments *arguments,
        bool autoUpdate,
        QObject *parent)
    : QGalleryResultSet(*new QGalleryTrackerResultSetPrivate(connection, arguments, autoUpdate), parent)
{
    Q_D(QGalleryTrackerResultSet);

    g_object_ref(G_OBJECT(d->connection));

    connect(&d->parserThread, SIGNAL(finished()), this, SLOT(_q_parseFinished()));

    d_func()->query();
}

QGalleryTrackerResultSet::QGalleryTrackerResultSet(
        QGalleryTrackerResultSetPrivate &dd,
        QObject *parent)
    : QGalleryResultSet(dd, parent)
{
    Q_D(QGalleryTrackerResultSet);

    g_object_ref(G_OBJECT(d->connection));

    connect(&d->parserThread, SIGNAL(finished()), this, SLOT(_q_parseFinished()));

    d_func()->query();
}

QGalleryTrackerResultSet::~QGalleryTrackerResultSet()
{
    Q_D(QGalleryTrackerResultSet);

    typedef QList<QGalleryTrackerMetaDataEdit *>::iterator iterator;
    for (iterator it = d->edits.begin(), end = d->edits.end(); it != end; ++it)
        (*it)->commit();

    d->parserThread.wait();

    g_object_unref(G_OBJECT(d->connection));
}

QStringList QGalleryTrackerResultSet::propertyNames() const
{
    return d_func()->propertyNames;
}

int QGalleryTrackerResultSet::propertyKey(const QString &property) const
{
    Q_D(const QGalleryTrackerResultSet);

    int index = d->propertyNames.indexOf(property);

    return index >= 0
            ? index + d->valueOffset
            : -1;
}

QGalleryProperty::Attributes QGalleryTrackerResultSet::propertyAttributes(int key) const
{
    return d_func()->propertyAttributes.value(key - d_func()->valueOffset);
}

QVariant::Type QGalleryTrackerResultSet::propertyType(int key) const
{
    return d_func()->propertyTypes.value(key - d_func()->valueOffset);
}

int QGalleryTrackerResultSet::itemCount() const
{
    return d_func()->rowCount;
}

int QGalleryTrackerResultSet::currentIndex() const
{
    return d_func()->currentIndex;
}

bool QGalleryTrackerResultSet::fetch(int index)
{
    Q_D(QGalleryTrackerResultSet);

    d->currentIndex = index;

    if (d->currentIndex < 0 || d->currentIndex >= d->rowCount) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        d->currentRow = d->rCache.values.begin();
#else
        d->currentRow = 0;
#endif
    } else if (d->currentIndex < d->iCache.cutoff) {
        d->currentRow = d->iCache.values.constBegin() + (d->currentIndex * d->tableWidth);
    } else {
        d->currentRow
                = d->rCache.values.constBegin()
                + ((d->currentIndex + d->rCache.offset - d->iCache.cutoff) * d->tableWidth);
    }

    Q_EMIT currentIndexChanged(d->currentIndex);
    Q_EMIT currentItemChanged();

    return d->currentRow != 0;
}

QVariant QGalleryTrackerResultSet::itemId() const
{
    Q_D(const QGalleryTrackerResultSet);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return d->currentRow->isNull()
               ? QVariant()
               : d->idColumn->value(d->currentRow);
#else
    return d->currentRow
            ? d->idColumn->value(d->currentRow)
            : QVariant();
#endif
}

QUrl QGalleryTrackerResultSet::itemUrl() const
{
    Q_D(const QGalleryTrackerResultSet);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return d->currentRow->isNull()
               ? QUrl()
               : d->urlColumn->value(d->currentRow).toUrl();
#else
    return d->currentRow
            ? d->urlColumn->value(d->currentRow).toUrl()
            : QUrl();
#endif
}

QString QGalleryTrackerResultSet::itemType() const
{
    Q_D(const QGalleryTrackerResultSet);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return d->currentRow->isNull()
               ? QString()
               : d->typeColumn->value(d->currentRow).toString();
#else
    return d->currentRow
            ? d->typeColumn->value(d->currentRow).toString()
            : QString();
#endif
}

QList<QGalleryResource> QGalleryTrackerResultSet::resources() const
{
    Q_D(const QGalleryTrackerResultSet);

    QList<QGalleryResource> resources;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (!d->currentRow->isNull()) {
#else
    if (d->currentRow) {
#endif
        const QUrl url = d->urlColumn->value(d->currentRow).toUrl();

        if (!url.isEmpty()) {
            QMap<int, QVariant> attributes;

            typedef QVector<int>::const_iterator iterator;
            for (iterator it = d->resourceKeys.begin(), end = d->resourceKeys.end();
                    it != end;
                    ++it) {
                QVariant value = metaData(*it);

                if (!value.isNull())
                    attributes.insert(*it, value);
            }

            resources.append(QGalleryResource(url, attributes));
        }
    }
    return resources;
}

QVariant QGalleryTrackerResultSet::metaData(int key) const
{
    Q_D(const QGalleryTrackerResultSet);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (d->currentRow->isNull() || key < d->valueOffset) {
#else
    if (!d->currentRow || key < d->valueOffset) {
#endif
        return QVariant();
    } else if (key < d->compositeOffset) {  // Value column.
        return *(d->currentRow + key);
    } else if (key < d->aliasOffset) {      // Composite column.
        return d->compositeColumns.at(key - d->compositeOffset)->value(d->currentRow);
    } else if (key < d->columnCount) {      // Alias column.
        return *(d->currentRow + d->aliasColumns.at(key - d->aliasOffset) + d->valueOffset);
    } else {
        return QVariant();
    }
}

bool QGalleryTrackerResultSet::setMetaData(int, const QVariant &)
{
    return false;
}

void QGalleryTrackerResultSet::cancel()
{
    d_func()->flags |= QGalleryTrackerResultSetPrivate::Cancelled;
    d_func()->flags &= ~QGalleryTrackerResultSetPrivate::Live;

    if (!(d_func()->flags &QGalleryTrackerResultSetPrivate::Active))
        QGalleryAbstractResponse::cancel();
}

bool QGalleryTrackerResultSet::waitForFinished(int msecs)
{
    Q_D(QGalleryTrackerResultSet);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QTimer timer;
#else
    QTime timer;
#endif
    timer.start();

    do {
        if (d->flags & QGalleryTrackerResultSetPrivate::Active) {
            if (d->waitForSyncFinish(msecs)) {
                d->parserThread.wait();

                d->_q_parseFinished();

                if (!(d->flags & QGalleryTrackerResultSetPrivate::Active))
                    return true;
            } else {
                return false;
            }
        } else if (d->flags & (QGalleryTrackerResultSetPrivate::Refresh)) {
            d->update();
        } else {
            return true;
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    } while ((msecs -= timer.remainingTime()) > 0);
#else
    } while ((msecs -= timer.restart()) > 0);
#endif
    return false;
}

bool QGalleryTrackerResultSet::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::UpdateRequest:
        d_func()->update();

        return true;
    case QEvent::UpdateLater:
        d_func()->processSyncEvents();

        return true;
    default:
        return QGalleryAbstractResponse::event(event);
    }
}

void QGalleryTrackerResultSet::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == d_func()->updateTimer.timerId()) {
        d_func()->update();

        event->accept();
   }
}

void QGalleryTrackerResultSet::refresh(const QList<int> &serviceIds)
{
    Q_D(QGalleryTrackerResultSet);

    for (int id : serviceIds) {
        if ((d->updateMask & id)
                && !d->updateTimer.isActive()
                && (d->flags & QGalleryTrackerResultSetPrivate::Live)) {


            d->flags |= QGalleryTrackerResultSetPrivate::Refresh;

            if (!(d->flags & QGalleryTrackerResultSetPrivate::Active)) {
                d->updateTimer.start(100, this);
            }
        }
    }
}

QT_END_NAMESPACE_DOCGALLERY

#include "moc_qgallerytrackerresultset_p.cpp"
