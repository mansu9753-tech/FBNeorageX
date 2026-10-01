#pragma once
// SlangPreset.h — RetroArch .slangp 프리셋 파서 (다중 패스)
//
//  프리셋은 "패스 목록 + 각 패스의 렌더 설정 + LUT 텍스처 + 파라미터 값" 이다.
//
//      shaders = 4
//      shader0 = shaders/newpixie/accumulate.slang
//      alias0  = accum1              ← 뒤 패스가 이 이름으로 샘플링한다
//      scale_type0 = source          ← 입력과 같은 크기로 렌더
//      filter_linear0 = true
//      textures = "frametexture"
//      frametexture = shaders/newpixie/crtframe.png
//
//  ★ #reference 상속
//      Mega Bezel / CyberLab 계열 프리셋은 본문이 거의 없고
//          #reference "../MBZ__3__STD.slangp"
//          HSM_ASPECT_RATIO_MODE = 2
//      처럼 부모 프리셋을 가리킨 뒤 값 몇 개만 덮어쓴다.
//      부모를 먼저 읽고 자식 값으로 덮는 방식으로 처리한다.
//      경로는 "그 키를 적어 놓은 파일" 기준으로 풀어야 한다. 부모와 자식이
//      서로 다른 폴더에 있는 경우가 흔하기 때문이다.

#include <QHash>
#include <QString>
#include <QVector>

// 렌더 타깃 크기를 정하는 방식
enum class SlangScale {
    Source,    // 입력 크기 × scale (기본)
    Viewport,  // 화면 크기 × scale
    Absolute   // 지정한 픽셀 크기
};

// 텍스처 가장자리 처리
enum class SlangWrap { ClampToEdge, ClampToBorder, Repeat };

struct SlangPass {
    QString    path;                       // .slang 절대경로
    QString    alias;                      // 뒤 패스가 참조할 이름 (없을 수 있음)

    // 가로/세로를 따로 지정하는 프리셋이 있다 (scale_type_x0 = viewport 등).
    //   지정이 없으면 둘 다 scale_type0 / scale0 을 따른다.
    SlangScale scaleTypeX = SlangScale::Source;
    SlangScale scaleTypeY = SlangScale::Source;
    float      scaleX     = 1.0f;
    float      scaleY     = 1.0f;
    bool       hasScale   = false;         // 프리셋이 크기를 명시했는가

    // 기존 단일 축 표기 (예전 코드 호환용 — scaleX/scaleTypeX 와 같은 값)
    SlangScale scaleType = SlangScale::Source;
    float      scale     = 1.0f;

    bool       filterLinear = false;
    SlangWrap  wrap = SlangWrap::ClampToBorder;

    // 정밀도가 필요한 패스(블러/블룸/누적)는 부동소수점 렌더타깃을 쓴다.
    //   8비트로 렌더하면 밝기 누적이 뭉개져 번짐이 지저분해진다.
    bool       floatFbo    = false;
    bool       srgbFbo     = false;
    bool       mipmapInput = false;

    int        frameCountMod = 0;          // FrameCount 를 이 값으로 나눈 나머지 (0=안 함)
};

struct SlangLut {
    QString   name;                        // 셰이더에서 쓰는 샘플러 이름
    QString   path;                        // 이미지 절대경로
    bool      linear = false;
    bool      mipmap = false;
    SlangWrap wrap = SlangWrap::ClampToBorder;
};

struct SlangPreset {
    bool               ok = false;
    QString            error;
    QVector<SlangPass> passes;
    QVector<SlangLut>  luts;
    // 프리셋이 지정한 파라미터 값 (셰이더의 #pragma parameter 기본값을 덮는다)
    QHash<QString, float> params;
    // #reference 로 실제로 읽어들인 파일 목록 (진단용)
    QStringList        sourceFiles;
};

// 파일에서 읽는다. #reference 상속을 따라간다. 이쪽이 정식 진입점이다.
SlangPreset parseSlangPresetFile(const QString& path);

// 텍스트만 넘기는 버전 (단위 테스트용). #reference 는 따라갈 수 없다.
SlangPreset parseSlangPreset(const QString& presetText, const QString& presetDir);
