%define greversion 22.0a1

Name:       xulrunner
Summary:    XUL runner
Version:    %{greversion}
Release:    1
Group:      Applications/Internet
License:    Mozilla License
URL:        http://www.mozilla.com
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(QtCore) >= 4.6.0
BuildRequires:  pkgconfig(QtOpenGL)
BuildRequires:  pkgconfig(QtGui)
BuildRequires:  pkgconfig(xt)
BuildRequires:  pkgconfig(pango)
BuildRequires:  pkgconfig(alsa)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(dbus-glib-1)
BuildRequires:  pkgconfig(gstreamer-0.10)
BuildRequires:  pkgconfig(gstreamer-app-0.10)
BuildRequires:  pkgconfig(gstreamer-plugins-base-0.10)
BuildRequires:  autoconf213
BuildRequires:  python
BuildRequires:  zip
BuildRequires:  unzip

%description
Mozilla XUL runner

%package devel
Group: Development/Tools/Other
Requires: xulrunner
Summary: Headers for xulrunner

%description devel
Development files for xulrunner.

%package misc
Group: Development/Tools/Other
Requires: xulrunner
Summary: Misc files for xulrunner

%description misc
Tests and misc files for xulrunner

%prep
%setup -q -n %{name}-%{version}

%build
export MOZCONFIG=embedding/embedlite/config/mozconfig.merqtxulrunner
%{__make} -f client.mk build_all %{?jobs:MOZ_MAKE_FLAGS="-j%jobs"}

%install
export MOZCONFIG=embedding/embedlite/config/mozconfig.merqtxulrunner
%{__make} -f client.mk install DESTDIR=%{buildroot}
%{__chmod} +x %{buildroot}%{_libdir}/xulrunner-%{greversion}/*.so

%files
%defattr(-,root,root,-)
%attr(755,-,-) %{_bindir}/*
%dir %{_libdir}/xulrunner-%{greversion}/dictionaries
%{_libdir}/xulrunner-%{greversion}/*.so
%{_libdir}/xulrunner-%{greversion}/omni.ja
%{_libdir}/xulrunner-%{greversion}/dependentlibs.list
%{_libdir}/xulrunner-%{greversion}/dictionaries/*

%files devel
%defattr(-,root,root,-)
%{_datadir}/*
%{_libdir}/xulrunner-devel-%{greversion}
%{_libdir}/pkgconfig
%{_includedir}/*

%files misc
%defattr(-,root,root,-)
%{_libdir}/xulrunner-%{greversion}/*
%exclude %{_libdir}/xulrunner-%{greversion}/*.so
%exclude %{_libdir}/xulrunner-%{greversion}/omni.ja
%exclude %{_libdir}/xulrunner-%{greversion}/dependentlibs.list
%exclude %{_libdir}/xulrunner-%{greversion}/dictionaries/*

%changelog
