CXX_MODULE = docgallery
TARGET     = docgalleryplugin
TARGETPATH = QtDocGallery

QT = \
    core \
    qml \
    docgallery

HEADERS += \
    qdeclarativedocumentgallery.h \
    qdeclarativegalleryfilter.h \
    qdeclarativegalleryitem.h \
    qdeclarativegalleryquerymodel.h \
    qdeclarativegallerytype.h

SOURCES += \
    qdeclarativedocumentgallery.cpp \
    qdeclarativegallery.cpp \
    qdeclarativegalleryfilter.cpp \
    qdeclarativegalleryitem.cpp \
    qdeclarativegalleryquerymodel.cpp \
    qdeclarativegallerytype.cpp

load(qml_plugin)
