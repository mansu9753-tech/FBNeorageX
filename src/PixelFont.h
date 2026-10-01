#pragma once
// PixelFont.h — 8x8 비트맵 폰트 (NeoRageX 메뉴 셸용)
//
//  원본 NeoRageX 는 8x8 비트맵 face 를 쓴다. 시스템 폰트를 렌더해서 알파를
//  1비트로 자르는 방법도 있지만, 그러면 Windows 와 스팀덱에 깔린 폰트가 달라
//  글자 모양이 서로 달라진다. 그래서 폰트 데이터를 코드에 그대로 넣었다.
//
//  글리프 데이터는 Daniel Hepper 의 font8x8_basic (public domain).
//  한 글자 8바이트, 한 바이트가 한 행이고 bit0 이 맨 왼쪽 픽셀이다.
//
//  셀은 8x10 이다. 글리프는 8x8 이고 셀 위쪽에 1px 내려서 놓는다.
//  (원본이 10px 간격의 셀에 8px face 를 얹은 배치와 같다)

#include <QColor>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QString>

class PixelFont {
public:
    static constexpr int CELL_W = 8;
    static constexpr int CELL_H = 10;
    static constexpr int GLYPH_Y = 1;    // 셀 안에서 글리프를 내려놓는 양

    // 문자열을 그린다.
    //   x, y   : 셀의 좌상단 (논리 픽셀)
    //   scale  : 정수 배율 (가로·세로 공통)
    //   scaleY : 세로만 추가 배율 (OPTIONS 의 2배 높이 face 용)
    //   retro 가 true 이면 통일 모드에서도 8x8 비트맵 글자로 그린다 (테두리 위 제목 등).
    void draw(QPainter& p, const QString& text, int x, int y,
              const QColor& color, int scale, int scaleY = 1, bool retro = false);

    // 통일 모드: 영문·숫자·기호도 한글과 같은 도트 폰트로 그려 두 글자의 크기를 맞춘다.
    //   8x8 글자는 한글(14px 도트)보다 훨씬 작아 한 줄에 섞이면 어색하다. 한국어 화면에서 켠다.
    void setUnified(bool on) { m_unified = on; }
    bool unified() const { return m_unified; }

    // 글자 한 조각(UTF-16 단위)이 차지하는 칸 수. 한글 같은 전각 글자는 두 칸이다.
    //   이모지처럼 두 조각으로 된 글자(surrogate pair)는 앞 조각만 두 칸으로 세고 뒤 조각은 0 칸이다.
    static int unitCells(QChar c) {
        if (c.isLowSurrogate()) return 0;
        return (c.unicode() >= 0x1100) ? 2 : 1;
    }

    // 도트 폰트에 없는 이모지·기호를 폰트에 있는 비슷한 기호로 바꾼다 (안 그러면 ?? 나 빈 칸이 나온다).
    static QString sanitize(const QString& text);

    // 문자열이 차지하는 칸 수. 한글 같은 전각 글자는 두 칸으로 센다.
    static int cells(const QString& text) {
        int n = 0;
        for (const QChar c : text) n += unitCells(c);
        return n;
    }

    // 문자열의 픽셀 폭
    static int width(const QString& text, int scale) {
        return cells(text) * CELL_W * scale;
    }

    // maxCells 칸에 들어가도록 자른다. 잘리면 끝을 '.' 으로 표시한다.
    static QString fit(const QString& text, int maxCells) {
        if (maxCells <= 0) return QString();
        if (cells(text) <= maxCells) return text;
        QString out;
        int used = 0;
        for (const QChar c : text) {
            const int w = unitCells(c);
            if (used + w > maxCells - 1) break;
            out += c;
            used += w;
        }
        return out + QLatin1Char('.');
    }

    // maxCells 칸에 들어가도록 앞을 자른다. 잘리면 앞을 '.' 으로 표시한다.
    //   경로처럼 끝이 중요한 문자열용이다 ("...ild_static/roms").
    static QString fitTail(const QString& text, int maxCells) {
        if (maxCells <= 0) return QString();
        if (cells(text) <= maxCells) return text;
        QString out;
        int used = 0;
        for (int i = int(text.size()) - 1; i >= 0; --i) {
            const int w = unitCells(text.at(i));
            if (used + w > maxCells - 1) break;
            out.prepend(text.at(i));
            used += w;
        }
        return QLatin1Char('.') + out;
    }

    // 8x8 face 는 소문자 g 를 9 처럼 그린다. 원본의 'Fi9hters' 가 그 흔적이다.
    static QString gTo9(const QString& s) {
        QString r = s;
        r.replace(QLatin1Char('g'), QLatin1Char('9'));
        return r;
    }

private:
    // 색깔별 아틀라스(1배율). 224칸 x 1행.
    const QImage& atlas(const QColor& color);
    QHash<QRgb, QImage> m_atlas;

    // 8x8 표에 없는 글자(한글 등)를 시스템 폰트로 그린 한 덩어리.
    //   화면에 바로 그리지 않고 여유 있는 이미지에 먼저 그린다. drawText 에 셀 크기의 사각형을
    //   주면 폰트의 줄 높이가 셀보다 클 때 글자 위아래가 잘려 나간다 (스팀덱에서 하단이 잘린 원인).
    struct KoRun { QImage img; int baselineY = 0; int advance = 0; int pad = 0; };
    const KoRun& koreanRun(const QString& run, const QColor& color, int px);
    QHash<QString, KoRun> m_ko;
    bool m_unified = false;
};
