Name:           dsr-uu
Version:        0.1
Release:        1
Summary:        An source routed routing protocol for ad hoc networks.

Group:          System Environment/Base
License:        GPL
Source:         dsr-uu-0.1.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

%{!?kernel: %{expand: %%define        kernel          %(uname -r)}}

%define       kversion        %(echo %{kernel} | sed -e s/smp// -)
%define       krelver         %(echo %{kversion} | tr -s '-' '_')

%if %(echo %{kernel} | grep -c smp)
        %{expand:%%define ksmp -smp}
%endif

%description 

DSR (Dynamic Source Routing) is a routing protocol for ad hoc
networks. It uses source routing and is being developed within the
MANET Working Group of the IETF.

%prep
%setup -q

%build
make RPM_OPT_FLAGS="$RPM_OPT_FLAGS"

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/usr/sbin
mkdir -p $RPM_BUILD_ROOT/lib/modules/%{kernel}/dsr-uu

install -s -m 755 dsr-uu.sh $RPM_BUILD_ROOT/usr/sbin/dsr-uu.sh        
install -m 644 dsr.ko $RPM_BUILD_ROOT/lib/modules/%{kernel}/dsr-uu/dsr.ko
install -m 644 linkcache.ko $RPM_BUILD_ROOT/lib/modules/%{kernel}/dsr-uu/linkcache.ko



%clean
rm -rf $RPM_BUILD_ROOT

%post
/sbin/depmod -a

%postun
/sbin/depmod -a

%files
%defattr(-,root,root)
%doc README ChangeLog

/usr/sbin/dsr-uu.sh
/lib/modules/%{kernel}/dsr-uu/dsr.ko
/lib/modules/%{kernel}/dsr-uu/linkcache.ko

%changelog
* Wed Aug 10 2005 Erik Nordstrom <erikn@wormhole.it.uu.se> - 0.1-1
- Created spec file

