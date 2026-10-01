#pragma once
// ArcadeLayout.h — 게임의 아케이드 버튼 배치 판별
//
//  FBNeo 는 게임 종류에 따라 아케이드 버튼을 retropad 인덱스에 다르게 배정한다.
//  그래서 같은 패드 버튼이라도 게임에 따라 다른 인덱스로 보내야 의도한
//  버튼이 눌린다. (retro_input.cpp 의 bStreetFighterLayout 분기)
//
//   ┌ Standard — 네오지오·파이널파이트·던전스 등 대부분의 게임
//   │    버튼1=0(B) 버튼2=8(A) 버튼3=1(Y) 버튼4=9(X) 버튼5=11(R) 버튼6=10(L)
//   │
//   └ SixButton — 스트리트파이터 계열 6버튼 격투 (LP MP HP / LK MK HK)
//        LP=1(Y)  MP=9(X)  HP=10(L)
//        LK=0(B)  MK=8(A)  HK=11(R)
//        3연타 매크로: 3xPunch=12(L2), 3xKick=13(R2)
//
//  ※ 위 인덱스는 FBNeo 소스에서 직접 확인했고, 사용자가 실기로 검증한
//    네오지오 표(10=CD, 11=AB, 12=BCD, 13=ABC)와도 일치한다.

#include <QString>
#include <QHash>

enum class PadLayout {
    Standard = 0,   // 네오지오/벨트스크롤 등 일반 배치
    SixButton       // 6버튼 격투 배치
};

// 이 롬이 6버튼 격투 배치를 쓰는가
bool      isSixButtonFighter(const QString& rom);
PadLayout padLayoutOf(const QString& rom);

// UI 표시용 이름 (한글/영문)
QString   padLayoutLabel(PadLayout l, bool english);

// ── 코어가 알려준 버튼 정의에서 6버튼 인덱스를 뽑는다 ────────
//   FBNeo 는 게임이 실행되고 입력을 처리하기 시작하면
//   retro_input_descriptor 로 "0=Weak Kick, 1=Weak Punch ..." 를 보낸다.
//   그 값이 유일한 정답이므로, 오면 하드코딩 대신 이걸 쓴다.
//   (롬 이름 목록은 게임 실행 전 미리보기용 추정치로만 남는다)
struct CoreSixButtons {
    bool valid = false;
    int lp = -1, mp = -1, hp = -1;
    int lk = -1, mk = -1, hk = -1;
};
CoreSixButtons parseCoreSixButtons(const QHash<int, QString>& desc);
