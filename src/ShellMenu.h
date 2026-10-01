#pragma once
// ShellMenu.h — OPTIONS 메뉴 컨트롤러
//
//  NeoRageXShell(그리기·입력)과 ShellPage(카테고리별 내용·동작)를 이어 준다.
//  하는 일은 딱 세 가지다.
//    1. 페이지를 등록하고 셸의 카테고리 목록을 만든다
//    2. 셸이 "열린다 / 값 조절 / 항목 선택" 을 알리면 해당 페이지의 행에 전달한다
//    3. 동작 뒤에 페이지의 행을 다시 만들어 셸에 밀어 넣는다
//  MainWindow 는 페이지를 등록하기만 하고 개별 카테고리의 동작을 알지 못한다.

#include <QObject>
#include <functional>
#include <memory>
#include <vector>

#include "ShellPage.h"

class NeoRageXShell;

class ShellMenu : public QObject {
    Q_OBJECT
public:
    ShellMenu(NeoRageXShell* shell, QObject* parent = nullptr);

    // 페이지를 등록한다. 등록 순서가 화면의 카테고리 순서다.
    //   label   : 메뉴에 보일 카테고리 이름
    //   enabled : false 면 흐리게 그리고 누를 수 없다 (예: 아직 준비 안 된 것)
    void addPage(std::unique_ptr<ShellPage> page, const QString& label,
                 bool enabled = true);

    // 등록을 마치고 셸에 카테고리 목록을 넘긴다. addPage 를 다 부른 뒤 한 번만.
    void install();

    // 열려 있는 카테고리의 행을 다시 그린다 (외부에서 설정이 바뀐 경우)
    void refreshOpen();

    // 표시 언어. korean() 이 true 이면 사전(ShellText)에 있는 문구를 한국어로 보여 준다.
    //   언어가 바뀌면 retranslate() 를 부른다 (카테고리·하단 버튼·검색창·열린 하위 행).
    void setKoreanProvider(std::function<bool()> korean) { m_korean = std::move(korean); }
    void retranslate();

private:
    struct Entry {
        std::unique_ptr<ShellPage> page;
        QString                    label;
        bool                       enabled = true;
        QVector<ShellPage::Row>    rows;     // 지금 화면에 올라가 있는 행
    };

    Entry* find(const QString& id);
    void   rebuild(Entry& e);     // 행을 새로 만들어 셸에 밀어 넣는다

    void onAboutToOpen(const QString& id);
    void onAdjusted(const QString& id, int index, int delta);
    void onChosen(const QString& id, int index);

    NeoRageXShell*      m_shell = nullptr;
    std::function<bool()> m_korean;
    QString tr(const QString& s) const;
    std::vector<Entry>  m_entries;
};
