#pragma once
// ShaderParamDialog.h — slang 셰이더 파라미터 편집 창
//
//  RetroArch 의 "셰이더 파라미터" 메뉴에 해당한다. 프리셋이 #pragma parameter 로
//  선언한 값들을 목록으로 보여주고 즉시 바꿔 볼 수 있게 한다.
//
//  이게 필요한 이유:
//    Mega Bezel 처럼 베젤을 직접 그리는 셰이더는 화면 배치를 자기 파라미터로
//    정한다. 예를 들어 HSM_ASPECT_RATIO_MODE 를 6(Full) 으로 두어야 게임이
//    화면 전체로 늘어난다. 그 값을 못 건드리면 프리셋 파일을 손으로 고치는
//    수밖에 없다.
//
//  Mega Bezel 은 파라미터가 900개가 넘는다. 그래서
//    · 검색창으로 걸러 보고
//    · 값 범위가 사실상 0 인 항목(예: "[ SCREEN SCALE ]:")은 제목으로 표시하고
//    · 바꾸는 즉시 화면에 반영한다
//  세 가지를 갖췄다.

#include <QDialog>
#include <QHash>
#include <QString>
#include <QVector>

#include "SlangCompile.h"   // SlangParamDecl

class GameViewIface;
class QLineEdit;
class QVBoxLayout;
class QWidget;
class QScrollArea;
class QLabel;

class ShaderParamDialog : public QDialog {
    Q_OBJECT
public:
    ShaderParamDialog(GameViewIface* canvas, const QString& shaderKey,
                      bool english, QWidget* parent = nullptr);

signals:
    // 값이 바뀔 때마다 알린다 (설정 저장은 MainWindow 가 한다)
    void parameterChanged(const QString& name, float value);
    void resetRequested();

private:
    void buildRows();
    void applyFilter(const QString& text);

    struct Row {
        SlangParamDecl decl;
        QWidget*       widget = nullptr;   // 이 파라미터가 차지하는 줄 전체
        QString        haystack;           // 검색 대상(이름+설명, 소문자)
        bool           isTitle = false;
    };

    GameViewIface* m_canvas = nullptr;
    QString       m_shaderKey;
    bool          m_en = false;
    QLineEdit*    m_search = nullptr;
    QLabel*       m_count = nullptr;
    QWidget*      m_listInner = nullptr;
    QVBoxLayout*  m_listLayout = nullptr;
    QVector<Row>  m_rows;
};
