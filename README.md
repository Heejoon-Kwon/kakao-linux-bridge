# Kakao Linux Bridge

공식 Windows용 카카오톡을 Ubuntu 데스크톱에서 실행하기 위한 독립 프로젝트입니다. Wine과 카카오톡은 별도로 설치됩니다. Bridge는 MIT 라이선스로 배포됩니다.

## 설치

Ubuntu 24.04 LTS amd64를 대상으로 합니다. 설치 전에 다른 카카오톡 인스턴스를 종료하세요.

```sh
sudo apt install ./kakao-linux-bridge_0.4.0_amd64.deb
```

앱 목록에서 **KakaoTalk (Bridge)**를 실행합니다. Bridge는 PATH의 Wine, 환경 변수 `WINEPREFIX`, Bridge 전용 prefix 및 기본 `~/.wine`에서 환경을 찾습니다. 설정 창에서 실행 파일과 경로를 지정할 수 있습니다. 카카오톡이 없으면 **Install KakaoTalk…**에서 공식 배포처로부터 받은 로컬 설치 파일을 선택하세요. 사용자 지정 또는 기존 prefix를 사용하는 경우 전용 prefix에 설치하려면 설정의 prefix 경로를 전용 prefix로 지정하세요. `doctor`로 탐지 결과를 확인할 수 있습니다.

```sh
kakao-linux-bridge doctor
kakao-linux-bridge install-client /path/to/KakaoTalk_Setup.exe
kakao-linux-bridge start
kakao-linux-bridge status
kakao-linux-bridge stop
```

`stop`은 동일 Wine prefix의 카카오톡에 정상 종료를 요청합니다. 다른 Windows 프로그램과 동시에 사용하는 경우 그 프로그램은 계속 실행됩니다. 트레이는 StatusNotifierItem 호스트가 있는 환경에서 표시되며 기본 클릭은 설정을 엽니다. 트레이가 없는 GNOME 환경에서는 앱 목록의 설정 및 종료 메뉴를 사용하세요. 표시 위치는 데스크톱 알림 서비스가 결정합니다.

## 화면과 파일 설정

- **Wine DPI**는 96–384를 지정합니다. 0은 현재 Wine 값을 유지합니다. 변경은 다음 실행 때 적용되며 해당 prefix의 다른 Windows 앱에도 영향을 줄 수 있습니다.
- 기본 모드는 XIM 설정을 유지하고, 설정이 없으면 IBus/Fcitx 환경 변수 또는 GNOME에 등록된 IBus 한글 입력 소스를 사용합니다. 명시적으로 **Ubuntu Korean input (IBus + X11/XWayland)**를 켜면 Bridge가 실행하는 Wine에 아래 프로필을 적용합니다.
- **Use Ubuntu Downloads**를 사용하면 전용 prefix의 Windows 다운로드 알려진 폴더를 XDG Downloads에 연결합니다. 카카오톡에서 자체 다운로드 경로를 설정한 경우 파일 창에서 `C:\Ubuntu Downloads`를 선택하세요. 매핑을 끄면 다음 실행에서 설정 적용을 생략합니다.
- **Start KakaoTalk at login**은 XDG 자동 시작 항목을 설정합니다.

## 기존 Wine 설치에 창과 글꼴 통합 적용

카카오톡과 같은 Wine prefix에서 실행 중인 다른 Windows 앱을 먼저 종료하세요. Bridge Settings에서 다음 항목을 각각 적용할 수 있습니다.

1. **Use Separate KakaoTalk Window**는 Wine 가상 데스크톱 지정값을 제거하고 X11 창 관리자가 카카오톡 창을 직접 관리하도록 설정합니다. Wine 가상 데스크톱을 사용하던 다른 앱도 같은 prefix를 공유하면 영향을 받을 수 있습니다.
2. **Apply Korean Font Aliases**는 설치된 나눔고딕 또는 Noto Sans CJK KR을 Wine의 누락된 굴림·돋움·바탕·맑은 고딕 글꼴 이름에 연결합니다. Wine에서 이미 해당 이름의 글꼴을 찾으면 그 글꼴을 우선합니다. 입력 조합은 아래 IBus 프로필에서 별도로 설정합니다.
3. **KakaoTalk (Bridge)**를 실행합니다. 같은 prefix의 Wine 데스크톱이 이미 실행 중이었다면, 그 prefix의 모든 Windows 앱을 닫고 Wine 세션이 끝난 뒤 다시 실행하세요.

터미널에서는 `kakao-linux-bridge integrate-window`와 `kakao-linux-bridge integrate-fonts`를 실행할 수 있습니다. 각 작업은 변경 전에 `HKCU\Software\Wine`을 XDG 상태 디렉터리의 별도 `before.reg` 파일에 저장하고 경로를 출력합니다. 이전 값을 복원하려면 해당 prefix에서 Wine Regedit의 가져오기 기능으로 그 파일을 여세요. 가져오기는 기존 값을 되돌리며 백업 이후 추가된 값은 별도로 제거해야 합니다. [Wine 가상 데스크톱 설정](https://github.com/wine-mirror/wine/blob/wine-10.0/dlls/win32u/winstation.c) · [Wine 글꼴 대체 동작](https://github.com/wine-mirror/wine/blob/wine-10.0/dlls/win32u/font.c)

## 이미지 붙여넣기

일반 복사·붙여넣기는 Wine과 데스크톱의 클립보드 연동을 사용합니다. 이미지가 붙지 않으면 복사한 뒤 Bridge Settings의 **Prepare Image Paste**를 누르고 붙여넣을 때까지 설정 창을 열어 두세요. 현재 이미지에 PNG와 BMP를 제공하고, BMP는 다른 앱이 요청할 때 생성합니다. 이 기능은 양방향에서 사용할 수 있으며, 클립보드에 이미지 데이터가 있는 경우에 적용됩니다.

## Ubuntu GNOME의 한글 입력 프로필

1. Ubuntu **설정 → 키보드 → 입력 소스**에 **한국어 (Hangul)**이 있는지 확인합니다. 없으면 `sudo apt install ibus-hangul fonts-nanum`으로 구성 요소를 설치한 뒤 입력 소스를 추가합니다.
2. Bridge Settings에서 **Ubuntu Korean input (IBus + X11/XWayland)**를 선택하고 저장합니다. 카카오톡을 종료한 뒤 Bridge에서 다시 실행합니다. 같은 Wine prefix에서 다른 앱을 켜 둔 경우, 모든 앱을 정상 종료하고 다시 실행하면 기존 Wine 데스크톱이 재사용되는 상황을 피할 수 있습니다.
3. `kakao-linux-bridge doctor`에서 X11/XWayland, XIM, IBus Hangul 스키마, 한국어 UTF-8 로케일을 확인합니다. 로케일이 없으면 `sudo locale-gen ko_KR.UTF-8`으로 생성할 수 있습니다.

프로필은 접근 가능한 X 디스플레이가 있을 때 Wine의 Wayland 드라이버를 비활성화하고 IBus를 지정합니다. 설치된 한국어 UTF-8 로케일을 사용할 때는 상속된 `LC_ALL`을 Wine 프로세스에서 제거합니다. 설정을 끄고 카카오톡을 재실행하면 기존 입력기와 렌더러 선택을 사용합니다. 기본적으로 Wine의 반복적인 `fixme` 로그만 줄이고 명시적으로 지정한 `WINEDEBUG` 설정은 유지합니다. [Ubuntu 24.04 카카오톡 입력 구성 사례](https://github.com/woogamer/kakaotalk-wine-ubuntu) · [Wine DLL 로드 순서](https://github.com/wine-mirror/wine/blob/wine-11.0/dlls/ntdll/unix/loadorder.c)

적용 후 채팅창에서 한글 조합과 확정을 각각 확인하세요. 카카오톡과 Ubuntu 편집기 사이에서 텍스트, 스크린샷 및 투명 PNG를 양방향으로 붙여 넣고, 카카오톡을 최소화한 상태에서 메시지 수신 알림을 확인합니다. Bridge 테스트는 IBus 및 로케일 환경 전달, 기존 DLL 설정 보존, X 디스플레이가 없을 때의 대체 실행을 확인합니다. 실제 채팅창의 조합 렌더링과 이미지 호환성은 확인이 필요합니다.

## 데스크톱 알림

1. Bridge Settings에서 **Desktop alerts for taskbar flashes (experimental)**를 선택하고 저장합니다.
2. 카카오톡을 종료한 뒤 Bridge에서 다시 실행합니다.
3. **Test Notification**은 데스크톱 표시 경로를 점검합니다. 카카오톡 창을 최소화하고 다른 계정에서 메시지를 보내 실제 수신 시 항목이 표시되는지 별도로 확인합니다.

Bridge는 Wine의 X11 드라이버가 보내는 작업 표시줄 점멸 이벤트를 구독합니다. Bridge가 실행한 카카오톡의 PID와 창 클래스가 일치하면 **카카오톡에서 확인할 항목이 있습니다.**를 데스크톱에 보냅니다. 이 이벤트는 메시지 수신 외에도 발생할 수 있으며, 점멸 요청을 만들지 않는 메시지는 감지할 수 없습니다. 메시지 본문과 발신자를 수집하지 않습니다. Wine이 X11 또는 XWayland를 사용하는 환경에서 동작하는 실험 기능으로 기본값은 꺼짐입니다. GNOME에서는 알림 배너가 화면 상단에 표시되며 시스템 알림 설정이 적용됩니다. [Wine 9.0 X11 드라이버](https://github.com/wine-mirror/wine/blob/wine-9.0/dlls/winex11.drv/window.c) · [GNOME 알림](https://help.gnome.org/gnome-help/shell-overview.html)

Wine 없이 알림만 받으려면 Android 휴대폰의 KDE Connect를 Ubuntu GNOME의 GSConnect에 페어링하고 휴대폰에서 알림 접근 권한을 준 뒤, 알림 동기화의 앱 목록에서 카카오톡을 활성화할 수 있습니다. 휴대폰이 생성한 알림을 전달하는 방식입니다. [KDE Connect](https://kdeconnect.kde.org/) · [GSConnect](https://github.com/GSConnect/gnome-shell-extension-gsconnect)

알림이 안 뜨는 경우 카카오톡을 먼저 종료한 뒤 `kakao-linux-bridge trace-events`로 실행하고, 창을 최소화한 상태에서 다른 계정으로 메시지를 보내 보세요. 터미널에 창 표시와 작업 표시줄 점멸 이벤트만 출력됩니다. 이 진단은 메시지·창 제목을 수집하지 않습니다.

## 검증 범위

세션 D-Bus에서 단일 감독 프로세스를 사용하며 Wine 실행기 종료를 비동기로 감시합니다. GTK4 설정 창은 별도 실행 파일입니다. Bridge는 주기적인 타이머나 네트워크 서비스 없이 동작합니다.

- **클립보드**는 Wine의 데스크톱 연동 경로를 사용합니다. 설정 창에서 테스트 텍스트 및 작은 이미지를 복사한 뒤 카카오톡에 붙여 넣으세요. 역방향은 카카오톡에서 텍스트와 이미지를 복사한 후 **Clipboard Formats**에서 형식 노출을 확인하고 Ubuntu 편집기에 붙여 넣습니다. 실제 카카오톡과 Wayland/XWayland 간 양방향 이미지 호환성은 미검증입니다.
- **알림**은 Xvfb와 가짜 알림 서비스에서 X11 이벤트부터 D-Bus 알림까지 테스트했습니다. 다른 PID·창 클래스의 이벤트, 점멸 종료 이벤트, 연속 요청이 배너를 중복 생성하지 않는지 확인했습니다. 공식 카카오톡이 실제 메시지를 받을 때 이 이벤트를 발생시키는지는 미검증입니다.
- **한글과 DPI**는 Ubuntu에서 실제 카카오톡에 로그인한 뒤 입력 중 조합 문자열과 확정 후 문자열을 확인하세요. DPI 96과 144에서 재실행해 크기 및 선명도를 비교하세요.
- 기본 설치, Quit 동작, KakaoTalk 손자 프로세스나 자체 업데이트 이후의 수명 추적은 실제 Wine 및 카카오톡에서 검증해야 합니다.

## 복구 및 제거

`kakao-linux-bridge repair`는 전용 prefix에서 Wine 초기화 갱신을 수행합니다. 기존 개인 prefix를 변경하지 않습니다. 설정 파일이 손상된 경우 `~/.config/kakao-linux-bridge/settings.ini`를 별도 위치에 보관하고 설정 창을 다시 여세요. 제거할 때는 다음 명령을 실행합니다. 사용자 설정과 전용 prefix는 패키지를 재설치할 때 사용할 수 있습니다.

```sh
kakao-linux-bridge stop
kakao-linux-bridge disable-autostart
sudo apt remove kakao-linux-bridge
```

## 빌드 및 성능 검증

```sh
sudo apt install build-essential cmake pkg-config libglib2.0-dev libgtk-4-dev libxcb1-dev dbus xvfb xclip
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -V
cpack --config build/CPackConfig.cmake -B build -G DEB
```

가짜 Wine 테스트는 탐지, 설치·복구 명령 전달, DPI, 다운로드 매핑, 중복 실행 방지, 트레이 D-Bus 등록 및 종료를 검증합니다. 실제 비교는 같은 Wine 버전·prefix·카카오톡 버전·디스플레이 세션에서 시작 전에 Wine과 카카오톡만 실행하여 기준선을 기록하고, Bridge를 적용해 반복합니다.

```sh
tests/sample-resources.sh idle 10 PID1 PID2
```

각 구간에 필요한 PID를 넣으세요. 실행 직후, 유휴, 메시지 수신, 이미지 복사·붙여넣기, 설정 창 실행·종료, 카카오톡 종료 후 및 장시간 실행을 각각 기록합니다. 프로세스별 RSS와 PSS를 별도 열로 출력합니다. 추가 상주 메모리는 Bridge 프로세스의 PSS 합계로 보고하고, RSS 합계는 성능 기준에 사용하지 않습니다. 명령은 측정 구간의 시작과 끝에만 샘플링하므로 짧은 피크는 별도의 순간 샘플로 포착해야 합니다. 오래 실행하거나 종료를 수반하는 시나리오는 PID가 존재하는 구간과 종료 후 프로세스 목록을 함께 기록하세요. 실제 기준선 및 전체 시나리오 결과는 아직 없습니다.

### 개발 환경 측정

Debian 13 amd64에서 가짜 Wine으로 클라이언트 실행 및 종료를 재현했습니다. 유휴 상태의 Bridge는 RSS 4,476 KiB, PSS 3,589 KiB였으며 2초 동안 CPU 틱 증가가 없었습니다. Xvfb에서 실행한 GTK4 설정 창은 한 시점에 RSS 69,364 KiB, PSS 58,487 KiB였고, 창을 닫자 프로세스가 종료됐습니다. 이 값은 가짜 Wine과 Xvfb를 사용한 측정값입니다. Ubuntu에서 실제 카카오톡을 사용한 상주 PSS 비교와 장시간 측정은 아직 필요합니다.

0.2.0에서 알림 감시를 켠 상태의 Xvfb 재현 테스트는 유휴 RSS 4,756 KiB, PSS 2,388 KiB였고 2초 동안 CPU 틱 증가가 없었습니다. 공유 페이지의 배분이 달라지는 개별 테스트이므로 앞선 측정치와 직접 비교할 수 없습니다.

0.4.0은 Debian의 Xvfb에서 실제 Wine 10.0과 Win32 테스트 앱으로 확인했습니다. Wine Desktop 안에 뜨던 테스트 앱이 창 통합 적용 후 별도 창으로 떴고, Windows의 `CF_DIB`와 Linux의 `image/bmp` 사이에서 양방향으로 이미지가 전달됐습니다. 굴림 글꼴 이름으로 한글 글리프를 읽었고, Win32 `FlashWindowEx` 호출에서 Bridge가 D-Bus 알림을 보냈습니다. 실제 카카오톡 메시지 수신 및 채팅창의 입력 조합과 붙여넣기는 별도로 확인해야 합니다.
