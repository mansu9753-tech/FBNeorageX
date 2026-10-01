// ShellPageDirectories.cpp — DIRECTORIES 카테고리
//
//  사용자가 바꿀 수 있는 폴더는 ROM 과 프리뷰 둘뿐이다. 나머지(저장·스크린샷·치트·
//  녹화·셰이더)는 프로그램 폴더 기준으로 고정이다(포터블 구조). 고정 폴더는 바꾸는
//  항목이 아니라 안내 줄로만 보여 준다.
//
//  폴더 선택은 OS 의 폴더 선택창을 쓴다. 폴더 탐색기를 셸 안에 새로 만드는 건
//  이득이 없다 (OS 창이 더 낫고, 스팀덱에서도 잘 동작한다).

#include "ShellPages.h"

#include <QDir>
#include <QFileDialog>

#include "AppSettings.h"
#include "PixelFont.h"

namespace {

class DirectoriesPage final : public ShellPage {
public:
    using ShellPage::ShellPage;
    QString id() const override { return QStringLiteral("dirs"); }

    QVector<Row> rows() override {
        QVector<Row> out = {
            Row::action(QStringLiteral("ROM FOLDER"),
                        [this] { pick(gSettings.romPath, en() ? "Select ROM folder" : "ROM 폴더 선택"); },
                        QStringLiteral("CHANGE...")),
            Row::path(gSettings.romPath),

            Row::action(QStringLiteral("PREVIEW FOLDER"),
                        [this] { pick(gSettings.previewPath, en() ? "Select preview folder" : "프리뷰 폴더 선택"); },
                        QStringLiteral("CHANGE...")),
            Row::path(gSettings.previewPath),

        };
        out.append(Row::note(en() ? "-- FIXED (NOT CHANGEABLE) --" : "-- 고정 경로 (변경 불가) --"));
        fixedRows(out);
        return out;
    }

private:
    // 긴 설명을 줄 폭(칸)에 맞춰 여러 안내 줄로 나눈다. 한 줄이 넘치면 뒤가 잘려 읽을 수 없다.
    static void wrapNotes(QVector<Row>& out, const QString& text, int maxCells) {
        QString line;
        for (const QString& w : text.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            const QString cand = line.isEmpty() ? w : line + QLatin1Char(' ') + w;
            if (!line.isEmpty() && PixelFont::cells(cand) > maxCells) {
                out << Row::note(line);
                line = w;
            } else {
                line = cand;
            }
        }
        if (!line.isEmpty()) out << Row::note(line);
    }

    // 사용자가 바꿀 수 없는 경로. 프로그램이 실제로 읽고 쓰는 위치를 이름·경로·설명으로 보여 준다.
    void fixedRows(QVector<Row>& out) {
        const QString base = AppSettings::baseDir();
#ifdef _WIN32
        const QString core = base + QStringLiteral("/fbneo_libretro.dll");
#else
        const QString core = base + QStringLiteral("/fbneo_libretro.so");
#endif
        struct Item { const char* nameKo; const char* nameEn; QString path; const char* ko; const char* en; };
        const Item items[] = {
            {"세이브", "SAVES", gSettings.savePath,
             "세이브 스테이트(F1~F8)와 코어의 게임 저장 데이터가 여기에 저장됩니다.",
             "Save states (F1-F8) and the core's game save data."},
            {"스크린샷", "SCREENSHOTS", gSettings.screenshotPath,
             "F12 로 찍은 스크린샷 (게임이름_시각.png).",
             "Screenshots taken with F12 (game_time.png)."},
            {"녹화", "RECORDINGS", gSettings.recordPath,
             "F9 로 녹화한 영상 (mp4).",
             "Videos recorded with F9 (mp4)."},
            {"치트", "CHEATS", gSettings.cheatPath,
             "치트 INI 파일 (게임이름.ini). 게임을 실행하면 자동으로 읽습니다.",
             "Cheat INI files (game.ini), loaded automatically when a game starts."},
            {"셰이더", "SHADERS", base + QStringLiteral("/shaders"),
             "RetroArch 셰이더 (.slangp .slang .glsl). 폴더 구조를 그대로 유지해서 넣으세요.",
             "RetroArch shaders (.slangp .slang .glsl). Keep their folder structure."},
            {"베젤", "BEZELS", base + QStringLiteral("/bezels"),
             "베젤 PNG. 게임이름.png, 기종.png, default.png 순으로 찾습니다.",
             "Bezel PNGs, looked up as game.png, platform.png, then default.png."},
            {"BIOS / 시스템", "BIOS / SYSTEM", gSettings.romPath,
             "BIOS 파일(neogeo.zip 등)은 ROM 폴더에 함께 두세요. 코어 치트는 그 안의 fbneo/cheats 를 씁니다.",
             "Keep BIOS files (neogeo.zip etc.) in the ROM folder. Core cheats use fbneo/cheats inside it."},
            {"에뮬레이터 코어", "EMULATOR CORE", core,
             "FBNeo 에뮬레이터 코어 파일. 프로그램과 같은 폴더에 있어야 합니다.",
             "The FBNeo emulator core. It must sit next to the program."},
            {"설정 파일", "SETTINGS FILE", base + QStringLiteral("/config.json"),
             "모든 설정(화면, 소리, 컨트롤, 즐겨찾기 등)이 저장되는 파일.",
             "Where all settings (video, audio, controls, favorites...) are saved."},
            {"게임 이름 DB", "GAME NAMES", base + QStringLiteral("/gamelist.xml"),
             "한글 게임 이름 데이터. 있으면 목록에 적용됩니다 (names.txt 도 같은 폴더).",
             "Localized game names, applied to the list when present (names.txt too)."},
            {"폴더 안내", "FOLDER GUIDE", base + QStringLiteral("/README.txt"),
             "각 폴더에 무엇을 넣는지 적어 둔 안내 파일.",
             "A note describing what goes into each folder."},
        };
        for (const Item& it : items) {
            out << Row::note(QString::fromUtf8(en() ? it.nameEn : it.nameKo));
            out << Row::path(it.path);
            wrapNotes(out, QString::fromUtf8(en() ? it.en : it.ko), 78);
        }
    }

    // 폴더를 고르면 그 자리에 저장하고, 앱에 "폴더가 바뀌었다" 고 알린다.
    //   ROM 폴더가 바뀌면 코어의 시스템 폴더(BIOS 탐색)와 게임 목록도 바뀌어야 한다.
    void pick(QString& target, const QString& title) {
        const QString dir = QFileDialog::getExistingDirectory(m_h.window, title, target);
        if (dir.isEmpty()) return;                 // 취소
        target = QDir::fromNativeSeparators(dir);
        gSettings.save();
        if (m_h.applyPaths) m_h.applyPaths();
        say(QStringLiteral("📁 ") + target);
    }
};

}  // namespace

std::unique_ptr<ShellPage> makeDirectoriesPage(const ShellHost& host) {
    return std::make_unique<DirectoriesPage>(host);
}
