Name:       harbour-stumblefish
Summary:    BeaconDB collector for Sailfish OS
Version:    0.1.0
Release:    2
License:    MIT
Group:      Qt/Qt
URL:        https://github.com/abranson/harbour-stumblefish
Source0:    %{name}-%{version}.tar.bz2

BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Positioning)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Sql)
BuildRequires:  pkgconfig(connman-qt5)
BuildRequires:  pkgconfig(qofonoext)
BuildRequires:  pkgconfig(sailfishapp)
BuildRequires:  desktop-file-utils
BuildRequires:  librsvg-tools
Requires:       sailfishsilica-qt5 >= 0.10.9

%description
Stumblefish collects opt-in Wi-Fi, cell tower, and Bluetooth beacon
observations with a position fix, stores the reports locally, and can submit
them to a configurable Geosubmit endpoint such as BeaconDB.

%prep
%autosetup

%build
%qmake5 VERSION='%{version}'
%make_build

%install
%qmake5_install

desktop-file-install --delete-original \
  --dir %{buildroot}%{_datadir}/applications \
  %{buildroot}%{_datadir}/applications/*.desktop

for size in 86 108 128 172; do
    icon_dir="%{buildroot}%{_datadir}/icons/hicolor/${size}x${size}/apps"
    mkdir -p "${icon_dir}"
    rsvg-convert -w ${size} -h ${size} \
        -o "${icon_dir}/%{name}.png" \
        src/icons/%{name}.svg
done

%pre
if [ "$1" -gt 1 ]; then
    systemctl-user stop %{name}d || true
fi

%post
systemctl-user daemon-reload || true
systemctl-user enable %{name}d || true
systemctl-user start %{name}d || true

%preun
if [ "$1" -eq 0 ]; then
    systemctl-user disable %{name}d || true
    systemctl-user stop %{name}d || true
fi

%postun
systemctl-user daemon-reload || true

%files
%{_bindir}/%{name}
%{_bindir}/%{name}d
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/dbus-1/services/org.stumblefish.Collector.service
%{_datadir}/icons/hicolor/86x86/apps/%{name}.png
%{_datadir}/icons/hicolor/108x108/apps/%{name}.png
%{_datadir}/icons/hicolor/128x128/apps/%{name}.png
%{_datadir}/icons/hicolor/172x172/apps/%{name}.png
%{_sysconfdir}/sailjail/permissions/Stumblefish.permission
%{_userunitdir}/%{name}d.service
