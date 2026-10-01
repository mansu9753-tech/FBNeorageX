#pragma once
// NeoRageXShell.h — NeoRageX 0.6b 메뉴 셸
//
//  GUI 를 이 위젯이 통째로 그린다. 창 크기 그대로 논리 좌표를 잡으므로
//  어떤 해상도에서도 레터박스가 생기지 않는다.
//
//  ── 이렇게 그리는 이유 (전부 느낌에 관여한다) ───────────────
//   · 테두리는 굵은 선이 아니라 1px 사각 링 10겹이다. 링마다 파란 램프 한 단계를
//     맡는다. 모서리가 계산 없이 저절로 각지게 물린다.
//   · 선은 와이프가 아니다. 박스마다 펜 하나가 좌상단에서 출발해 둘레를 한 번에
//     돈다(약 4초). 링들은 같은 '비율'이 아니라 같은 '픽셀 속도'로 전진해야
//     파이프 끝이 비스듬해지지 않는다.
//   · 8비트 팔레트에는 알파 블렌딩이 없었다. 페이드는 단계로 스냅되고 등장은
//     하드컷이다.
//   · 이징이 없다. 전부 선형. 서브픽셀도 없다.
//   · 논리 프레임레이트를 60fps 로 고정한 누산기를 쓴다.
//
//  ── 본문 글자 ────────────────────────────────────────────
//   제목은 크롬 배율(s)로, 목록·이벤트·옵션 본문은 한 단계 작은 배율로 그린다.
//   8x8 면을 2배로 키우면 점 하나가 2x2 덩어리가 되어 굵고 거칠게 읽힌다.
//   한 단계 낮추면 점이 1px 이 되어 가늘어지고 한 화면에 더 많이 들어온다.

#include <QElapsedTimer>
#include <QCursor>
#include <QImage>
#include <QInputMethodEvent>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "NeoRageXSkin.h"
#include "PixelFont.h"

class QTimer;

class NeoRageXShell : public QWidget {
    Q_OBJECT
public:
    // 하위 항목.
    //   value 가 있으면 "라벨 ... 값" 으로 보여준다.
    //   adjustable 이면 값 양옆에 ◀ ▶ 를 달아 좌우로 고르게 한다.
    struct SubItem {
        QString label;
        QString value;
        bool    adjustable = false;
        bool    info = false;       // 안내 줄: 흐리게 그리고 선택·동작에서 제외
        bool    heading = false;    // 구역 제목: 밝은 색으로 그린다 (info 와 함께 쓴다)
        bool    elideLeft = false;  // 라벨이 길면 앞을 자른다 (경로용)
    };

    struct OptionEntry {
        QString          id;
        QString          label;
        bool             enabled = true;
        QVector<SubItem> sub;   // 비어 있으면 올라가기 연출 뒤 곧장 optionChosen
    };

    // 게임 목록 위쪽의 필터 단추 (ALL / FAV / 기종…)
    struct FilterTab {
        QString id;
        QString label;
    };

    // ── 표시 언어 ─────────────────────────────────────────
    //   셸은 받은 글자를 그대로 그린다. 번역은 밖(ShellMenu)에서 하고 여기엔 결과만 넘긴다.
    void setOptionLabels(const QStringList& labels);     // 옵션 항목 이름 (등록 순서대로)
    void setButtonLabels(const QStringList& labels);     // LAUNCH / IMPORT / EXIT
    void setSearchPlaceholder(const QString& t) { m_searchHint = t; update(); }
    void setBackLabel(const QString& t) { m_backLabel = t; update(); }
    // 한국어일 때 영문도 한글과 같은 도트 폰트로 그린다 (글자 크기를 맞춘다)
    void setUnifiedFont(bool on) { m_font.setUnified(on); update(); }
    void setPointerCursor(const QCursor& c) { m_pointer = c; setCursor(c); }

    // 검색어. 필터 단추 바로 아래 검색창에 보인다.
    void    setSearchText(const QString& t) { m_search = t; update(); }
    QString searchText() const { return m_search; }

    // ── 조작 ─────────────────────────────────────────────
    //   키보드·게임패드·(마우스 일부)가 전부 이 하나의 경로로 들어온다.
    //   입력 장치마다 분기를 따로 만들면 동작이 조금씩 어긋나기 때문이다.
    //   메뉴가 열려 있으면 메뉴를, 아니면 게임 목록을 조작한다.
    enum class Nav {
        Up, Down, Left, Right,
        PageUp, PageDown, Home, End,
        Accept,     // 실행 / 항목 선택
        Back,       // 메뉴 닫기
        Favorite,   // 즐겨찾기 토글
        Search,     // 검색어 입력창 열기 (게임패드용: 화면 키보드)
        ZoneLeft,       // L 버튼: 커서를 한 칸 왼쪽으로 (옵션 → 게임 목록 → 필터)
        ZoneRight,      // R 버튼: 커서를 한 칸 오른쪽으로 (필터 → 게임 목록 → 옵션)
    };
    // 처리했으면 true. false 면 호출한 쪽이 다른 용도(예: ESC 로 종료)로 써도 된다.
    bool navigate(Nav a);

    explicit NeoRageXShell(QWidget* parent = nullptr);
    ~NeoRageXShell() override;

    // ── 내용 ─────────────────────────────────────────────
    void setGames(const QStringList& games, const QVector<bool>& favorites = {});
    void log(const QString& line);
    void setBackdrop(const QImage& img);
    void setPreview(const QImage& img);
    void setOptions(const QVector<OptionEntry>& opts);
    void setFilterTabs(const QVector<FilterTab>& tabs, const QString& currentId);

    const QStringList& games() const { return m_games; }
    int      selectedIndex() const { return m_sel; }
    QString  selectedGame()  const;
    void     setSelectedIndex(int i, bool notify = false);

    // ── 동작 ─────────────────────────────────────────────
    void boot();
    void skipBoot();
    void stop();
    bool isIdle() const;

    void openMenu(int i);
    void closeMenu();
    bool menuOpen() const { return m_menuFrame >= 0; }

    void setSubItems(const QString& categoryId, const QVector<SubItem>& items);
    QString openCategoryId() const;

signals:
    void menuAboutToOpen(const QString& categoryId);

    void launchRequested(const QString& game);
    void importRequested();
    void exitRequested();
    void gameSelected(const QString& game, int index);
    void gameHighlighted(const QString& game, int index);
    void favoriteToggled(int index);
    void filterChosen(const QString& filterId);
    void searchChanged(const QString& text);    // 검색어가 바뀔 때마다
    void searchRequested();                     // 게임패드로 검색창을 열려 할 때

    // 하위 항목 자체를 눌렀을 때 (동작형 항목)
    void optionChosen(const QString& id, int subIndex);
    // ◀ ▶ 로 값을 옮길 때. delta 는 -1 또는 +1.
    void subAdjusted(const QString& id, int subIndex, int delta);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void leaveEvent(QEvent*) override;
    void inputMethodEvent(QInputMethodEvent*) override;      // 한글 등 IME 입력
    QVariant inputMethodQuery(Qt::InputMethodQuery) const override;

private:
    // ── 타임라인 ─────────────────────────────────────────
    struct Phases { int bg, box, hold, tit; };
    Phases phases() const;
    int  bootEnd() const;
    int  contentFrame() const;
    bool seen(int cf, int at) const { return cf >= at; }
    void tick();

    // ── 배율 / 치수 ──────────────────────────────────────
    int  bodyScale() const { return qMax(1, m_L.s - 1); }   // 본문은 한 단계 작게
    int  bodyCellW() const { return nrx::ATLAS_CELL  * bodyScale(); }
    int  bodyCellH() const { return nrx::ATLAS_CELLH * bodyScale(); }
    int  listPitch() const { return bodyCellH() + 5 * m_L.s; }
    int  filterBarH() const;         // 필터 단추 줄 + 검색창
    int  filterRowsH() const;        // 필터 단추 줄만
    QRect searchRect() const;
    bool hitSearch(const QPoint& lp) const;
    bool hitSearchClear(const QPoint& lp) const;
    void setSearchFocus(bool on);

    // ── 스크롤바 (마우스로 끌고 누르기) ───────────────────────
    struct SBar {
        bool on = false;
        QRect track, up, down, thumb;
        int total = 0, rows = 0, top = 0;
    };
    SBar listBar() const;                       // 게임 목록
    SBar subBar() const;                        // 열린 옵션의 하위 목록
    bool pressScrollbar(const QPoint& lp);      // 스크롤바를 눌렀으면 true
    void scrollBy(int which, int delta);        // which: 0 목록, 1 하위 목록
    void dragScrollbar(const QPoint& lp);
    void startRepeat(int which, int delta);
    void editSearch(const QString& text);     // 검색어를 바꾸고 알린다
    void drawSearchBar(QPainter& p);
    QRect listArea() const;          // 필터 줄을 뺀 목록 영역
    int  rowsVisible() const;
    int  eventRowsVisible() const;

    // ── 그리기 ───────────────────────────────────────────
    void renderSurface();
    void tracePipe(QPainter& p, const QRect& b, double t);
    void ringRun(QPainter& p, const QRect& b, int o, double d);
    void drawFilterBar(QPainter& p);
    void drawList(QPainter& p);
    void drawListScrollbar(QPainter& p);
    void drawSpinner(QPainter& p, int x, int y, int w, int h, int dir, bool hot);
    void drawButtons(QPainter& p);
    void drawOptionsIdle(QPainter& p);
    void drawOptionsOpen(QPainter& p);
    void drawEvents(QPainter& p);
    void drawPreview(QPainter& p);
    void text(QPainter& p, const QString& s, int x, int y,
              const QColor& c, int scale, int scaleY = 1, bool retro = false);
    int  textW(const QString& s, int scale) const;
    double quant(double t) const;

    // ── 옵션 좌표 ────────────────────────────────────────
    int  optPitch() const;
    int  optFirstY() const;
    int  optY(int i) const;
    int  optX(const QString& label, int scale) const;
    QRect subArea() const;            // 하위 목록이 들어가는 영역
    int  subPitch() const;
    int  subRowsVisible() const;
    int  subRowY(int row) const;      // row 는 화면상의 줄(스크롤 반영 전)
    QRect subSpinRect(int row, int dir) const;   // dir -1 왼쪽, +1 오른쪽

    // ── 히트 테스트 ──────────────────────────────────────
    QPoint toLogical(const QPoint& widgetPt) const;
    int  hitOption(const QPoint& lp) const;
    int  hitButton(const QPoint& lp) const;
    int  hitGame(const QPoint& lp) const;
    int  hitSub(const QPoint& lp) const;         // 하위 항목 번호(절대)
    int  hitSubSpin(const QPoint& lp, int& dirOut) const;
    int  hitFilter(const QPoint& lp) const;

    void chooseResolution();
    bool navigateMenu(Nav a);
    bool navigateList(Nav a);
    void ensureSelectionVisible();
    void ensureSubVisible();
    void snapSubSel(int dir = +1);   // 안내 줄이 아닌 가까운 항목으로 옮긴다
    bool subSelectable(int i) const;
    bool subSelectableFrom(int i) const;

    // ── 상태 ─────────────────────────────────────────────
    nrx::Layout m_L;
    QImage      m_surface;
    mutable PixelFont m_font;

    QStringList   m_games, m_events;
    QVector<bool> m_favorites;
    QVector<OptionEntry> m_options;
    QVector<FilterTab>   m_filters;
    QString     m_filterCur;
    enum class Zone { Filter, List, Options };
    Zone        m_zone = Zone::List;      // 커서가 있는 곳 (L/R 버튼으로 옮긴다)
    QString     m_backLabel = QStringLiteral("BACK");
    QCursor     m_pointer;
    int         m_hoverBack = 0;          // 하위 메뉴의 뒤로 가기 단추 위에 마우스가 있는가
    bool        menuOpenDone() const;
    QRect       backRect() const;         // 하위 메뉴 아래쪽 가운데의 뒤로 가기 단추
    int         backReserve() const;      // 그 단추가 차지하는 세로 공간
    int         m_optSel = 0;             // 옵션 메뉴의 커서
    QStringList m_btnLabels;              // 번역된 버튼 이름 (비어 있으면 기본)
    QString     m_searchHint = QStringLiteral("SEARCH");
    int         m_dragBar = -1, m_dragOff = 0;      // 스크롤바 끌기
    int         m_repBar = -1,  m_repDelta = 0;     // 화살표를 누르고 있을 때의 반복
    QTimer*     m_repTimer = nullptr;
    QString     m_search;                 // 검색어
    QString     m_preedit;                // IME 조합 중인 글자
    bool        m_searchFocus = false;    // 검색창에 글자를 치고 있는가
    QImage      m_backdrop, m_preview;

    int  m_frame = 0;
    int  m_menuFrame = -1;
    int  m_openIdx = -1;
    int  m_hoverOpt = -1, m_hoverBtn = -1, m_hoverSub = -1, m_hoverFilter = -1;
    int  m_hoverSpin = 0;             // -1 / 0 / +1
    int  m_sel = -1;
    int  m_top = 0;                   // 목록 스크롤
    int  m_subTop = 0;                // 하위 항목 스크롤
    int  m_subSel = 0;                // 키보드로 고르는 하위 항목
    bool m_running = false;

    QTimer*       m_timer = nullptr;

    // ── 긴 게임 이름 자동 스크롤 (선택한 줄만) ─────────────────
    //   이름이 칸보다 길면 커서를 옮기고 잠시 뒤 왼쪽으로 흘러 뒷글자를 보여 준다.
    mutable int   m_marqSel  = -1;   // 스크롤 중인 게임 번호
    mutable int   m_marqStep = 0;    // 그 게임을 고른 뒤 지난 프레임(60fps 스텝)
    int  marqueeOverflow(int gi) const;              // 칸을 넘치는 폭(px). 0 이면 스크롤 없음
    int  marqueeOffset(int overflow) const;          // 지금 프레임의 왼쪽 이동량(px)
    QElapsedTimer m_clock;
    double        m_accum = 0.0;
};
