// PadRawBits.cpp — 원시 버튼 비트의 표시 이름

#include "PadRawBits.h"

QString padButtonName(int rawBit) {
    const uint32_t b = uint32_t(rawBit);

    // 트리거 (양 플랫폼 공통)
    if (b == PAD_L2) return QStringLiteral("L2  (왼쪽 트리거)");
    if (b == PAD_R2) return QStringLiteral("R2  (오른쪽 트리거)");

    if (b == PAD_A)     return QStringLiteral("A");
    if (b == PAD_B)     return QStringLiteral("B");
    if (b == PAD_X)     return QStringLiteral("X");
    if (b == PAD_Y)     return QStringLiteral("Y");
    if (b == PAD_LB)    return QStringLiteral("L1  (LB)");
    if (b == PAD_RB)    return QStringLiteral("R1  (RB)");
    if (b == PAD_BACK)  return QStringLiteral("SELECT  (Back)");
    if (b == PAD_START) return QStringLiteral("START");
    if (b == PAD_GUIDE) return QStringLiteral("GUIDE  (홈)");
    if (b == PAD_L3)    return QStringLiteral("L3  (왼쪽 스틱 누름)");
    if (b == PAD_R3)    return QStringLiteral("R3  (오른쪽 스틱 누름)");

    if (b == PAD_DPAD_UP)    return QStringLiteral("D-Pad 위");
    if (b == PAD_DPAD_DOWN)  return QStringLiteral("D-Pad 아래");
    if (b == PAD_DPAD_LEFT)  return QStringLiteral("D-Pad 왼쪽");
    if (b == PAD_DPAD_RIGHT) return QStringLiteral("D-Pad 오른쪽");

    // 스틱 비트가 없는 플랫폼(Windows)에서는 값이 0 이라 위 비교에 걸리지 않는다
    if (PAD_ST_UP    && b == PAD_ST_UP)    return QStringLiteral("왼쪽 스틱 위");
    if (PAD_ST_DOWN  && b == PAD_ST_DOWN)  return QStringLiteral("왼쪽 스틱 아래");
    if (PAD_ST_LEFT  && b == PAD_ST_LEFT)  return QStringLiteral("왼쪽 스틱 왼쪽");
    if (PAD_ST_RIGHT && b == PAD_ST_RIGHT) return QStringLiteral("왼쪽 스틱 오른쪽");

    // 표에 없는 버튼은 "버튼 N" 으로 (16진수보다 알아보기 쉽다)
    if (b != 0 && (b & (b - 1)) == 0) {
        int n = 0;
        for (uint32_t v = b; v > 1; v >>= 1) ++n;
        if (n < 16) return QStringLiteral("버튼 %1").arg(n);
    }
    return QStringLiteral("알 수 없음 (0x%1)").arg(rawBit, 0, 16);
}
