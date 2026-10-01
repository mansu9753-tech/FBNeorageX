#pragma once
// SlangCompile.h — RetroArch .slang (Vulkan GLSL 450) → OpenGL GLSL 330 변환
//
//  기존 SlangShader.cpp 는 문자열 치환으로 GLSL 120 을 만들었다. 그 방식은
//  UBO/배열/정수연산/textureLod 같은 GLSL 450 기능을 표현할 수 없어서
//  Mega Bezel 계열은 원천적으로 돌릴 수 없었다.
//
//  여기서는 RetroArch 와 같은 경로를 쓴다:
//      .slang  --(전처리: #include, #pragma stage)-->  Vulkan GLSL
//              --(glslang)-->  SPIR-V
//              --(SPIRV-Cross)-->  GLSL 330 core  +  리플렉션
//
//  리플렉션으로 얻는 것:
//    · UBO / push_constant 블록의 크기와 멤버별 오프셋
//      → 유니폼 이름을 하나하나 조회하지 않고 버퍼 한 방에 올린다.
//    · 샘플러 이름 목록 → Original / Source / PassOutput3 … 시맨틱 해석용
//
//  두 라이브러리 모두 C API 만 쓴다. MSYS2(glslang 16) 와
//  Ubuntu 24.04(glslang 15) 처럼 버전이 달라도 C API 는 호환되기 때문이다.

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

// UBO / push_constant 블록 안의 멤버 하나
struct SlangMember {
    QString  name;      // 예: "MVP", "SourceSize", "HSM_ASPECT_RATIO_MODE"
    quint32  offset = 0;
    quint32  size   = 0;   // 바이트
};

// 유니폼 블록 하나 (UBO 또는 push_constant)
struct SlangBlock {
    bool                 present = false;
    QString              glslName;   // SPIRV-Cross 가 실제로 뽑아낸 블록 이름
    quint32              binding = 0;
    quint32              size    = 0;   // 바이트 (std140 기준 전체 크기)
    QVector<SlangMember> members;

    const SlangMember* find(const QString& n) const {
        for (const SlangMember& m : members) if (m.name == n) return &m;
        return nullptr;
    }
};

// 셰이더가 선언한 사용자 파라미터 (#pragma parameter)
struct SlangParamDecl {
    QString name, desc;
    float   def = 0.f, min = 0.f, max = 1.f, step = 0.01f;
};

struct SlangCompiled {
    bool    ok = false;
    QString error;

    QByteArray vertexGlsl;      // #version 330 core
    QByteArray fragmentGlsl;

    SlangBlock ubo;             // set=0 binding=0 계열 UBO
    SlangBlock push;            // push_constant (GL 에서는 또 하나의 UBO 로 나온다)

    QStringList textures;       // 샘플러 이름 (선언 순서)
    QVector<SlangParamDecl> parameters;

    QString pragmaName;         // #pragma name  (별칭)
    QString pragmaFormat;       // #pragma format R16G16B16A16_SFLOAT 등
};

// path 의 .slang 파일을 읽어 GLSL 로 변환한다.
//   glslVersion: 뽑아낼 GLSL 버전. 실제 OpenGL 컨텍스트가 지원하는 최고 버전을
//     넣으면 된다. textureGather(component) 처럼 400 이상이라야 표현되는 기능이
//     있어서, 330 으로 고정하면 일부 셰이더가 변환 단계에서 막힌다.
SlangCompiled compileSlangFile(const QString& path, int glslVersion = 330);

// 파일을 읽지 않는 버전 (단위 테스트용). srcDir 은 #include 해석 기준 폴더.
SlangCompiled compileSlangSource(const QString& source, const QString& srcDir,
                                 const QString& displayName, int glslVersion = 330);
