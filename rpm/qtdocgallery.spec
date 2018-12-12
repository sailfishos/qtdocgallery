Name:       qt5-qtdocgallery
Summary:    Qt document gallery optional module
Version:    5.0.0
Release:    1
Group:      System/Libraries
License:    LGPLv2
URL:        https://github.com/mer-packages/qtdocgallery
Source0:    %{name}-%{version}.tar.bz2
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Test)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(tracker-sparql-1.0)

%description
Qt document gallery optional module

%package devel
Summary:    Development files for qtdocgallery
Group:      Development/Libraries
Requires:   %{name} = %{version}-%{release}

%description devel
This package contains the files necessary to develop
applications using qtdocgallery

%prep
%setup -q -n %{name}-%{version}

%build
touch .git
%qmake5  \
    tracker_enabled=yes \
    MODULE_VERSION=5.0.0
make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%qmake_install

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_libdir}/libQt5DocGallery.so.*
%{_libdir}/qt5/qml/QtDocGallery/*

%files devel
%defattr(-,root,root,-)
%{_includedir}/qt5/QtDocGallery/*
%{_libdir}/libQt5DocGallery.so
%{_libdir}/pkgconfig/*.pc
%{_libdir}/libQt5DocGallery.prl
%{_libdir}/libQt5DocGallery.la
%{_libdir}/cmake/Qt5DocGallery/Qt5DocGalleryConfig.cmake
%{_libdir}/cmake/Qt5DocGallery/Qt5DocGalleryConfigVersion.cmake
%{_datadir}/qt5/mkspecs/modules/qt_lib_docgallery.pri
%{_datadir}/qt5/mkspecs/modules/qt_lib_docgallery_private.pri
