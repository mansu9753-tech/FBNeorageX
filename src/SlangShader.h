#pragma once
// SlangShader.h — RetroArch .slang 셰이더를 데스크톱 GLSL 로 번역
//
//  .slang 은 Vulkan GLSL 이다. RetroArch 는 glslang 으로 SPIR-V 를 만들고
//  SPIRV-Cross 로 각 백엔드 언어를 뽑아낸다. 그 두 라이브러리는 수 MB 짜리
//  의존성이라 여기 붙이기 어렵다.
//
//  대신 "소스 수준 번역"을 한다. 단일 패스 셰이더가 쓰는 Vulkan 전용 문법은
//  종류가 많지 않아서, 그것만 데스크톱 GLSL 로 바꿔주면 그대로 컴파일된다.
//
//    · #version 450            → #version 130
//    · layout(std140, ...) uniform UBO { ... } global;
//                              → 멤버를 각각 uniform 으로 펼치고 "global." 제거
//    · layout(push_constant) uniform Push { ... } params;   (위와 동일)
//    · layout(location = N) / layout(set = N, binding = M)  → 제거
//    · #pragma format / #pragma name                        → 제거
//
//  ※ 다중 패스(.slangp 에서 shaders 가 2 이상)는 프레임버퍼 체인·피드백·LUT 이
//    필요해 이 방식으로는 안 된다. 그런 프리셋은 명확히 거절한다.

#include <QString>
#include <QHash>

struct SlangTranslation {
    bool    ok = false;
    QString vert;        // 번역된 정점 셰이더
    QString frag;        // 번역된 프래그먼트 셰이더
    QString error;       // ok=false 일 때 사유 (사용자에게 보여줄 문장)
};

// .slang 원본 → 데스크톱 GLSL 한 쌍
SlangTranslation translateSlang(const QString& source);

// .slangp 프리셋에서 "단일 패스" 셰이더 경로를 얻는다.
//   presetDir 기준 상대경로를 해석해 절대경로로 돌려준다.
//   다중 패스거나 형식이 이상하면 빈 문자열 + err 에 사유.
QString slangPresetSinglePass(const QString& presetText,
                              const QString& presetDir,
                              QString* err);
