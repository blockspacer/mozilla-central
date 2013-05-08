%define greversion 23.0a1

Name:       xulrunner
Summary:    XUL runner
Version:    %{greversion}
Release:    1
Group:      Applications/Internet
License:    Mozilla License
URL:        http://hg.mozilla.org/mozilla-central
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
BuildRequires:  pkgconfig(nspr) >= 4.9.6
BuildRequires:  pkgconfig(nss) >= 3.14.3
BuildRequires:  autoconf213
BuildRequires:  python
BuildRequires:  python-devel
BuildRequires:  zip
BuildRequires:  unzip
BuildRequires:  hunspell-devel
%ifarch i586 i486 i386
BuildRequires:  yasm
%endif
BuildRequires:  fdupes

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
export DONT_POPULATE_VIRTUALENV=1
export PYTHONPATH=$PWD/python:$PWD/config:$PWD/build:$PWD/xpcom/typelib/xpt/tools
for i in $(find $PWD/python $PWD/testing/mozbase -mindepth 1 -maxdepth 1 -type d); do
  export PYTHONPATH+=:$i
done
export SBOX_REDIRECT_FORCE=/usr/bin/python
# hack for when not using virtualenv
ln -sf $PWD/obj-build-mer-qt-xr/config.status $PWD/build/config.status

cp -rf embedding/embedlite/config/mozconfig.merqtxulrunner mozconfig

%ifarch %arm
echo "ac_add_options --with-arm-kuser" >> mozconfig
echo "ac_add_options --with-float-abi=toolchain-default" >> mozconfig
# No need for this, this should be managed by toolchain
echo "ac_add_options --with-thumb=toolchain-default" >> mozconfig
%endif
echo "mk_add_options MOZ_MAKE_FLAGS='-j%jobs'" >> mozconfig
echo "export LD=ld.gold" >> mozconfig
echo "ac_add_options --disable-tests" >> mozconfig
echo "ac_add_options --enable-system-hunspell" >> mozconfig
echo "ac_add_options --disable-strip" >> mozconfig
echo "ac_add_options --disable-mochitest" >> mozconfig
echo "ac_add_options --disable-installer" >> mozconfig
echo "ac_add_options --disable-javaxpcom" >> mozconfig
echo "ac_add_options --disable-crashreporter" >> mozconfig
export MOZCONFIG=mozconfig
%{__make} -f client.mk build STRIP="/bin/true" %{?jobs:MOZ_MAKE_FLAGS="-j%jobs"}

%install
export DONT_POPULATE_VIRTUALENV=1
export PYTHONPATH=$PWD/python:$PWD/config:$PWD/build:$PWD/xpcom/typelib/xpt/tools
for i in $(find $PWD/python $PWD/testing/mozbase -mindepth 1 -maxdepth 1 -type d); do
  export PYTHONPATH+=:$i
done
SBOX_REDIRECT_FORCE=/usr/bin/python

export MOZCONFIG=mozconfig
%{__make} -f client.mk install DESTDIR=%{buildroot}
%{__chmod} +x %{buildroot}%{_libdir}/xulrunner-%{greversion}/*.so
%fdupes -s %{buildroot}%{_includedir}
%fdupes -s %{buildroot}%{_libdir}
chmod +x %{buildroot}%{_libdir}/xulrunner-%{greversion}/*.so
# Use the system hunspell dictionaries
%{__rm} -rf ${RPM_BUILD_ROOT}%{_libdir}/xulrunner-%{greversion}/dictionaries
ln -s %{_datadir}/myspell ${RPM_BUILD_ROOT}%{_libdir}/xulrunner-%{greversion}/dictionaries

%files
%defattr(-,root,root,-)
%attr(755,-,-) %{_bindir}/*
%{_libdir}/xulrunner-%{greversion}/*.so
%{_libdir}/xulrunner-%{greversion}/omni.ja
%{_libdir}/xulrunner-%{greversion}/dependentlibs.list
%{_libdir}/xulrunner-%{greversion}/dictionaries

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
