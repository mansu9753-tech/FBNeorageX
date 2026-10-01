// ShellPageShots.cpp — SHOTS FACTORY 카테고리
//
//  스크린샷·프리뷰 이미지·녹화·프리뷰 영상. 전부 앱의 기존 동작을 부르기만 한다.
//  (게임이 실행 중이 아니면 앱 쪽에서 알아서 안내 로그를 남긴다)

#include "ShellPages.h"

namespace {

class ShotsPage final : public ShellPage {
public:
    using ShellPage::ShellPage;
    QString id() const override { return QStringLiteral("shots"); }

    QVector<Row> rows() override {
        QVector<Row> out;
        out << Row::note(QStringLiteral("SCREENSHOT"));
        out << Row::action(QStringLiteral("TAKE SCREENSHOT"),
                           [this] { if (m_h.takeScreenshot) m_h.takeScreenshot(); },
                           QStringLiteral("F12"));
        out << Row::note(QStringLiteral("screenshots/{rom}_{timestamp}.png"));
        out << Row::action(en() ? "SAVE AS PREVIEW IMAGE" : "프리뷰 이미지로 저장",
                           [this] { if (m_h.savePreviewShot) m_h.savePreviewShot(); },
                           QStringLiteral("CTRL+F12"));
        out << Row::note(en() ? "previews/{rom}.png (OVERWRITES)" : "previews/{rom}.png (덮어씀)");

        out << Row::action(QStringLiteral("FRAME LAB"),
                           [this] { if (m_h.openFrameLab) m_h.openFrameLab(); });
        out << Row::note(en() ? "STEP FRAME BY FRAME AND SAVE ANY FRAME (GAME MUST BE RUNNING)"
                              : "프레임을 하나씩 넘겨 보고 원하는 장면을 저장 (게임 실행 중일 때)");

        out << Row::note(QStringLiteral("VIDEO RECORD"));
        const bool rec = m_h.isRecording && m_h.isRecording();
        out << Row::action(QStringLiteral("RECORD"),
                           [this] { if (m_h.toggleRecording) m_h.toggleRecording(); },
                           rec ? QStringLiteral("STOP") : QStringLiteral("START") + QStringLiteral("  F9"));
        out << Row::note(QStringLiteral("recordings/{rom}_{timestamp}.mp4"));
        out << Row::action(en() ? "RECORD PREVIEW VIDEO" : "프리뷰 영상 녹화",
                           [this] { if (m_h.togglePreviewRecord) m_h.togglePreviewRecord(); },
                           QStringLiteral("CTRL+F9"));
        out << Row::note(en() ? "PRESS AGAIN TO SAVE previews/{rom}.mp4 (OVERWRITES)"
                              : "다시 누르면 previews/{rom}.mp4 저장 (덮어씀)");
        return out;
    }
};

}  // namespace

std::unique_ptr<ShellPage> makeShotsPage(const ShellHost& host) {
    return std::make_unique<ShotsPage>(host);
}
