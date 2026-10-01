// ShellText.cpp — 셸 메뉴 문구의 한국어 번역
//
//  메뉴 이름은 아케이드풍 영문(SCALE MODE 같은)으로 짜여 있고, 각 페이지는 그 영문을
//  그대로 내보낸다. 한국어 모드에서는 여기서 한 번에 바꿔 준다.
//  · 페이지마다 번역을 끼워 넣으면 항목을 고칠 때마다 두 곳을 손봐야 해서 어긋난다.
//    사전 한 곳에 모아 두면 새 항목은 여기에 한 줄만 더하면 된다.
//  · 사전에 없는 문구(코어가 주는 DIP 이름, 게임 이름 등)는 그대로 둔다.
//  · 이미 페이지가 언어를 보고 직접 만든 문구(en() ? "..." : "...")는 영문 사전에 없으므로
//    영향을 받지 않는다.

#include "ShellText.h"

#include <QHash>

namespace {

const QHash<QString, QString>& dict() {
    static const QHash<QString, QString> d = {
        // 카테고리 (메인 옵션은 뜻이 아니라 영문 소리를 옮긴다: 음역. 끝의 s 는 소리 내지 않는다)
        {"CONTROLS", "컨트롤"}, {"DIRECTORIES", "디렉터리"}, {"VIDEO OPTIONS", "비디오 옵션"},
        {"AUDIO OPTIONS", "오디오 옵션"}, {"MACHINE SETTINGS", "머신 세팅"},
        {"SHOTS FACTORY", "샷 팩토리"}, {"CHEATS", "치트"}, {"MULTIPLAYER", "멀티플레이어"},
        {"SYSTEM", "시스템"},
        // 하단 버튼 / 검색
        {"LAUNCH", "실행"}, {"IMPORT", "갱신"}, {"BACK", "뒤돌아가기"}, {"EXIT", "종료"}, {"SEARCH", "검색"},

        // VIDEO
        {"SCALE MODE", "화면 비율"}, {"SMOOTH FILTER", "부드럽게"}, {"CRT SCANLINE", "CRT 스캔라인"},
        {"CRT LEVEL", "CRT 강도"}, {"FLASH GUARD", "플래시 감소"}, {"FLASH LEVEL", "플래시 강도"},
        {"BEZEL", "베젤"}, {"VSYNC", "수직 동기"}, {"RENDERER", "렌더러"}, {"FULLSCREEN", "전체화면"}, {"FRAMESKIP", "프레임스킵"},
        {"SHADER", "셰이더"}, {"SHADER PARAMS", "셰이더 설정"},
        {"FULL", "전체"}, {"NONE", "없음"},
        // AUDIO
        {"VOLUME", "볼륨"}, {"SOUND SCOPE", "사운드 적용 범위"}, {"SOUND MODE", "사운드 모드"},
        {"SAMPLE RATE", "샘플레이트"}, {"BUFFER", "버퍼"},
        // MACHINE
        {"SAVE SCOPE", "저장 범위"},
        // DIRECTORIES
        {"ROM FOLDER", "ROM 폴더"}, {"PREVIEW FOLDER", "프리뷰 폴더"}, {"CHANGE...", "변경..."},
        // CHEATS
        {"ENABLE ALL", "전체 켜기"}, {"DISABLE ALL", "전체 끄기"}, {"LOAD INI FILE...", "INI 파일 불러오기..."},
        // SHOTS
        {"TAKE SCREENSHOT", "스크린샷 찍기"}, {"FRAME LAB", "프레임 랩"}, {"RECORD", "녹화"}, {"VIDEO RECORD", "동영상 녹화"},
        // SYSTEM
        {"SAVE SLOT", "저장 슬롯"}, {"SAVE STATE", "상태 저장"}, {"LOAD STATE", "상태 불러오기"},
        {"SCREENSHOT", "스크린샷"}, {"FAST FORWARD", "빨리 감기"}, {"1P / 2P PORT", "1P / 2P 포트"},
        {"TATE ROTATE", "세로 화면 회전"}, {"RESET GAME", "게임 리셋"}, {"STOP GAME", "게임 종료"},
        {"STARTUP INTRO", "시작 오프닝"}, {"LANGUAGE", "언어"}, {"KOREAN", "한국어"}, {"ENGLISH", "영어"},
        {"RESET DEFAULTS", "기본값으로 초기화"}, {"PRESS AGAIN TO CONFIRM", "한 번 더 누르면 실행"},
        // CONTROLS
        {"DEVICE", "장치"}, {"KEYBOARD", "키보드"}, {"GAMEPAD", "게임패드"}, {"ARCADE STICK", "아케이드 스틱"},
        {"TARGET PAD", "대상 패드"}, {"GAMEPAD MODE", "게임패드 모드"},
        {"RESET KEYBOARD", "키보드 초기화"}, {"RESET GAMEPAD", "게임패드 초기화"},
        {"RESET ARCADE STICK", "아케이드 스틱 초기화"},
        {"UP", "위"}, {"DOWN", "아래"}, {"LEFT", "왼쪽"}, {"RIGHT", "오른쪽"}, {"START", "시작"},
        {"SELECT / COIN", "셀렉트 / 코인"},
        // MULTIPLAYER
        {"HOST", "호스트"}, {"JOIN", "참가"}, {"PORT", "포트"}, {"DELAY", "딜레이"},
        {"HOST GAME", "방 만들기"}, {"ROOM CODE", "룸 코드"}, {"START GAME (HOST)", "게임 시작 (호스트)"},
        {"OR DIRECT IP", "또는 직접 IP"}, {"JOIN GAME", "참가하기"}, {"DISCONNECT", "연결 해제"},
        {"RELAY", "릴레이"}, {"RELAY SERVER", "릴레이 서버"}, {"YOUR IP", "내 IP"},
        {"BUILT-IN", "내장"}, {"CUSTOM", "커스텀"},
        {"THIS GAME", "이 게임"}, {"THIS PLATFORM", "이 기종"}, {"ALL GAMES", "모든 게임"},
        // 공통 값
        {"ON", "켜짐"}, {"OFF", "꺼짐"}, {"AUTO", "자동"},
    };
    return d;
}

// 뒤에 이름이 붙는 문구 (앞부분만 번역)
struct Prefix { const char* en; const char* ko; };
const Prefix kPrefix[] = {
    {"TURBO PERIOD", "터보 주기"},
    {"TURBO ", "터보 "},
    {"PLAYER  ", "플레이어  "},
};

}  // namespace

QString shellText(const QString& s, bool korean) {
    if (!korean || s.isEmpty()) return s;
    const auto it = dict().find(s);
    if (it != dict().end()) return it.value();
    for (const Prefix& p : kPrefix) {
        const QString pe = QString::fromLatin1(p.en);
        if (s.startsWith(pe)) return QString::fromUtf8(p.ko) + s.mid(pe.size());
    }
    return s;
}
