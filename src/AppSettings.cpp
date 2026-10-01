// AppSettings.cpp — 설정 로드/저장 (JSON config.json)

#include "AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDebug>
#include <QTextStream>

// ── 싱글톤 ────────────────────────────────────────────────
AppSettings& AppSettings::instance() {
    static AppSettings s;
    return s;
}

// ── 기준 폴더: 프로그램이 있는 폴더 (포터블) ─────────────────
//  Windows : FBNeoRageX.exe 가 있는 폴더
//  Linux   : 번들 루트 (실행파일이 <루트>/bin/ 이면 그 상위 = FBNeoRageX.sh 위치)
QString AppSettings::baseDir() {
    const QString appDir = QCoreApplication::applicationDirPath();
#ifndef _WIN32
    // 스팀덱 번들 구조 대응: .../FBNeoRageX/bin/FBNeoRageX → .../FBNeoRageX
    if (QFileInfo(appDir).fileName() == QLatin1String("bin"))
        return QDir(appDir + "/..").absolutePath();
#endif
    return appDir;
}

// 기본 경로 확정: 플랫폼 공통으로 모든 데이터를 프로그램 폴더 아래에 둔다.
//   생성자에서 하지 않는 이유 — gSettings 는 QApplication 보다 먼저 생성되고,
//   그 시점의 applicationDirPath() 는 빈 문자열이라 "/roms" 가 되어버린다.
void AppSettings::initDefaults() {
    const QString base = baseDir();
    romPath        = base + "/roms";
    previewPath    = base + "/previews";
    screenshotPath = base + "/screenshots";
    savePath       = base + "/saves";
    cheatPath      = base + "/cheats";
    recordPath     = base + "/recordings";
}

// 이 게임에 쓸 베젤 이미지 경로 (없으면 빈 문자열)
QString AppSettings::bezelPathFor(const QString& rom, const QString& platform,
                                  QString* whyOut) const {
    const QString dir = baseDir() + "/bezels";

    // 저장된 값이 파일명뿐이면 bezels/ 아래로 본다
    auto resolve = [&](const QString& v) -> QString {
        if (v.isEmpty()) return QString();
        const QString p = QFileInfo(v).isAbsolute() ? v : (dir + "/" + v);
        return QFile::exists(p) ? p : QString();
    };
    auto take = [&](const QString& path, const char* why) {
        if (whyOut) *whyOut = QString::fromUtf8(why);
        return path;
    };

    QString p;
    if (!rom.isEmpty()) {
        p = resolve(bezelAssign.value("game:" + rom));
        if (!p.isEmpty()) return take(p, "게임별");
        p = resolve(rom + ".png");
        if (!p.isEmpty()) return take(p, "게임별");
    }
    if (!platform.isEmpty()) {
        p = resolve(bezelAssign.value("plat:" + platform));
        if (!p.isEmpty()) return take(p, "기종별");
        p = resolve(platform + ".png");
        if (!p.isEmpty()) return take(p, "기종별");
    }
    p = resolve(bezelAssign.value("all"));
    if (!p.isEmpty()) return take(p, "전체");
    p = resolve("default.png");
    if (!p.isEmpty()) return take(p, "전체");

    if (whyOut) whyOut->clear();
    return QString();
}

// ── 데이터 폴더 생성 + 첫 실행 안내 ─────────────────────────
//   프로그램만 배포해도 사용자가 폴더를 손수 만들 필요가 없게 한다.
void AppSettings::ensureDataDirs() const {
    const QString base = baseDir();
    if (base.isEmpty()) return;

    // 게임을 하기 위한 기본 구성
    const QStringList dirs = {
        romPath, previewPath, screenshotPath, savePath, cheatPath, recordPath,
        base + "/shaders",              // 사용자 셰이더
        base + "/bezels",               // 베젤(아케이드 프레임) PNG
        base + "/system/fbneo/cheats",  // FBNeo 네이티브 치트 엔진이 읽는 위치
    };
    for (const QString& d : dirs)
        if (!d.isEmpty()) QDir().mkpath(d);

    // 어떤 폴더에 무엇을 넣는지 한 장으로 안내 (최초 1회만 생성)
    const QString guide = base + "/README.txt";
    if (!QFile::exists(guide)) {
        QFile f(guide);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            ts.setEncoding(QStringConverter::Utf8);
            ts << "FBNeoRageX - folder guide / 폴더 안내" << Qt::endl
               << "=========================================" << Qt::endl << Qt::endl
               << "roms/         Game ROM zip files      게임 롬(zip) 파일" << Qt::endl
               << "previews/     Preview image/video     프리뷰 이미지(.png)/영상(.mp4)" << Qt::endl
               << "screenshots/  Screenshots             스크린샷 저장" << Qt::endl
               << "saves/        Save states             세이브스테이트 저장" << Qt::endl
               << "cheats/       Cheat .ini files        치트 파일(.ini)" << Qt::endl
               << "recordings/   Video recordings        녹화 영상 저장" << Qt::endl
               << "shaders/      Custom shaders          사용자 셰이더(.glsl/.slang)" << Qt::endl
               << "bezels/       Bezel overlay PNG       베젤 이미지(롬이름.png)" << Qt::endl
               << "system/       Core data (auto)        코어가 쓰는 데이터(자동)" << Qt::endl
               << Qt::endl
               << "Put the libretro core next to the program:" << Qt::endl
               << "코어 파일은 프로그램과 같은 폴더에 두세요:" << Qt::endl
#ifdef _WIN32
               << "  fbneo_libretro.dll" << Qt::endl
#else
               << "  bin/fbneo_libretro.so" << Qt::endl
#endif
               << Qt::endl
               << "Preview files are matched by ROM name." << Qt::endl
               << "프리뷰는 롬 이름과 같은 파일명으로 넣으면 자동으로 표시됩니다." << Qt::endl
               << "  ex) roms/sf2ce.zip  ->  previews/sf2ce.png , previews/sf2ce.mp4" << Qt::endl;
        }
    }
}

AppSettings::AppSettings() {
    // 여기서는 경로를 만들지 않는다 (위 주석 참조).
    // main() 에서 QApplication 생성 직후 initDefaults() 가 호출된다.
}

QString AppSettings::defaultConfigPath() const {
    // 포터블: 프로그램 폴더의 config.json
    const QString path = baseDir() + "/config.json";

#ifndef _WIN32
    // ── 이전 버전(XDG) 설정 자동 이관 ──────────────────────────
    //  예전 Linux 빌드는 ~/.local/share/FBNeoRageX/FBNeoRageX/config.json 을
    //  썼다. 새 위치에 아직 파일이 없고 예전 파일이 있으면 한 번 복사해
    //  기존 설정(경로/키매핑/즐겨찾기 등)을 잃지 않게 한다.
    if (!QFile::exists(path)) {
        const QString oldData =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        const QString oldPath = oldData + "/config.json";
        if (QFile::exists(oldPath) && QFile::copy(oldPath, path))
            qDebug() << "[AppSettings] 이전 설정 이관:" << oldPath << "→" << path;
    }
#endif
    return path;
}

// ── JSON 헬퍼 ─────────────────────────────────────────────
template<typename T>
static T jval(const QJsonObject& o, const QString& key, T def) {
    if (!o.contains(key)) return def;
    QJsonValue v = o[key];
    if constexpr (std::is_same_v<T, QString>)
        return v.toString(def);
    else if constexpr (std::is_same_v<T, bool>)
        return v.toBool(def);
    else if constexpr (std::is_same_v<T, int>)
        return v.toInt(static_cast<int>(def));
    else if constexpr (std::is_same_v<T, double>)
        return v.toDouble(static_cast<double>(def));
    return def;
}

// ── 로드 ─────────────────────────────────────────────────
void AppSettings::load(const QString& path) {
    QString p = path.isEmpty() ? defaultConfigPath() : path;
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "AppSettings: config.json 없음 → 기본값 사용";
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "AppSettings: JSON 파싱 오류:" << err.errorString();
        return;
    }

    QJsonObject o = doc.object();

    romPath        = jval(o, "rom_path",            romPath);
    previewPath    = jval(o, "preview_path",        previewPath);
    screenshotPath = jval(o, "screenshot_path",     screenshotPath);
    savePath       = jval(o, "save_path",           savePath);
    cheatPath      = jval(o, "cheat_path",          cheatPath);
    recordPath     = jval(o, "record_path",         recordPath);

#ifndef _WIN32
    // ── 이전 버전(XDG) 경로를 새 포터블 경로로 교정 ─────────────
    //  예전 Linux 빌드의 config.json 에는 ~/.local/share/... , ~/Pictures/... ,
    //  ~/Videos/... , ~/ROMs/fbneo 가 저장돼 있다. 그대로 두면 설정을 이관해도
    //  파일이 계속 옛 위치에 쌓여 "프로그램 옆에 모인다"는 원칙이 깨진다.
    //  → 옛 위치를 가리키는 항목만 새 기준 폴더로 되돌린다.
    //    (사용자가 직접 지정한 제3의 경로는 건드리지 않는다)
    {
        const QString base    = baseDir();
        const QString oldData = QStandardPaths::writableLocation(
                                    QStandardPaths::AppLocalDataLocation);
        const QString oldPics = QStandardPaths::writableLocation(
                                    QStandardPaths::PicturesLocation) + "/FBNeoRageX";
        const QString oldVids = QStandardPaths::writableLocation(
                                    QStandardPaths::MoviesLocation) + "/FBNeoRageX";
        const QString oldRoms = QStandardPaths::writableLocation(
                                    QStandardPaths::HomeLocation) + "/ROMs/fbneo";

        auto fixPath = [&](QString& p, const QString& oldPrefix, const char* sub) {
            if (!oldPrefix.isEmpty() && p.startsWith(oldPrefix))
                p = base + "/" + QLatin1String(sub);
        };
        fixPath(previewPath,    oldData, "previews");
        fixPath(savePath,       oldData, "saves");
        fixPath(cheatPath,      oldData, "cheats");
        fixPath(screenshotPath, oldPics, "screenshots");
        fixPath(recordPath,     oldVids, "recordings");
        fixPath(romPath,        oldRoms, "roms");
    }
#endif

    audioVolume    = jval(o, "audio_volume",        audioVolume);
    audioSampleRate= jval(o, "audio_sample_rate",   audioSampleRate);
    audioBufferMs  = jval(o, "audio_buffer_ms",     audioBufferMs);
    soundMode      = jval(o, "sound_mode",          soundMode);
    bezelEnabled   = jval(o, "bezel_enabled",       bezelEnabled);

    videoScaleMode  = jval(o, "video_scale_mode",   videoScaleMode);
    // v1.9 마이그레이션: "Fill"이 기본값이었던 구버전 config를 "Fit"으로 자동 업그레이드
    // (사용자가 명시적으로 Fill을 선택한 경우 "video_scale_explicit" 키가 존재함)
    bool needMigrationSave = false;
    if (videoScaleMode == "Fill" && !o.contains("video_scale_explicit")) {
        videoScaleMode = "Fit";
        needMigrationSave = true;
    }
    videoSmooth     = jval(o, "video_smooth",        videoSmooth);
    videoCrtMode    = jval(o, "video_crt_mode",      videoCrtMode);
    videoCrtIntensity = jval(o, "video_crt_intensity", videoCrtIntensity);
    videoFrameskip  = jval(o, "video_frameskip",    videoFrameskip);
    videoFlashGuard    = jval(o, "video_flash_guard",    videoFlashGuard);
    videoFlashStrength = jval(o, "video_flash_strength", videoFlashStrength);
    videoVsync      = jval(o, "video_vsync",         videoVsync);
    videoShaderPath = jval(o, "video_shader_path",  videoShaderPath);

    uiLanguage        = jval(o, "ui_language",          uiLanguage);
    showIntro         = jval(o, "show_intro",           showIntro);
    videoRenderer     = jval(o, "video_renderer",       videoRenderer);
    netplayPort       = jval(o, "netplay_port",         netplayPort);
    netplayInputDelay = jval(o, "netplay_input_delay",  netplayInputDelay);
    netplayRelayUrl   = jval(o, "netplay_relay_url",    netplayRelayUrl);
    turboPeriod       = jval(o, "turbo_period",         turboPeriod);
    turboButtons    = jval(o, "turbo_buttons",       turboButtons);

    // 즐겨찾기 (JSON 배열)
    favorites.clear();
    for (const QJsonValue& v : o["favorites"].toArray())
        favorites.append(v.toString());
    lastGame = jval(o, "last_game", lastGame);

    // 컨트롤러
    inputMode = jval(o, "input_mode", inputMode);
    auto loadIntMap = [&](const QString& key, QHash<int,int>& dst) {
        dst.clear();
        QJsonObject mo = o[key].toObject();
        for (auto it = mo.begin(); it != mo.end(); ++it)
            dst[it.key().toInt()] = it.value().toInt();
    };
    loadIntMap("xinput_mapping",   xinputMapping);
    loadIntMap("winmm_mapping",    winmmMapping);
    loadIntMap("keyboard_mapping", keyboardMapping);
    loadIntMap("keyboard_mapping_six", keyboardMappingSix);

    // 기종별/게임별 컨트롤 매핑: { scope: { key: idx } }
    auto loadScoped = [&](const QString& key,
                          QHash<QString,QHash<int,int>>& dst) {
        dst.clear();
        QJsonObject so = o[key].toObject();
        for (auto it = so.begin(); it != so.end(); ++it) {
            QHash<int,int> m;
            QJsonObject mo = it.value().toObject();
            for (auto jt = mo.begin(); jt != mo.end(); ++jt)
                m[jt.key().toInt()] = jt.value().toInt();
            dst[it.key()] = m;
        }
    };
    loadScoped("kb_scoped", kbScoped);
    loadScoped("xi_scoped", xiScoped);
    loadScoped("wm_scoped", wmScoped);
    loadScoped("pad_profiles", padProfiles);   // (구버전)
    loadScoped("pad_maps",     padMaps);       // 단일 저장소

    // ── 구버전 패드 설정 1회 정리 ──────────────────────────────
    //   예전 구조는 저장 위치가 세 군데로 나뉘어 서로 덮어썼다. 그대로 두면
    //   새 구조에서도 어느 값이 이길지 예측할 수 없어, 패드 매핑만 비운다.
    //   (키보드·아케이드스틱·즐겨찾기 등 나머지 설정은 그대로 둔다)
    // ── 게임별/기종별 패드 매핑 정리 ──────────────────────────
    //   저장 범위를 "장치별" 하나로 단순화했다. 예전 game:/plat: 항목이 남아
    //   있으면 특정 게임에서만 다른 매핑이 튀어나와 꼬인다.
    {
        QStringList drop;
        for (auto it = padMaps.constBegin(); it != padMaps.constEnd(); ++it)
            if (it.key().startsWith("game:") || it.key().startsWith("plat:"))
                drop << it.key();
        for (const QString& k : drop) padMaps.remove(k);
        if (!drop.isEmpty()) padMapsMigrated = true;
    }

    // ── 인덱스로 저장된 6버튼 표 정리 ─────────────────────────
    //   6버튼 매핑은 이제 "의미"(약손/중손/…)로 저장한다. 인덱스는 게임마다
    //   달라서, 예전에 인덱스로 저장된 표는 다른 게임에서 반드시 어긋난다.
    {
        QStringList drop;
        for (auto it = padMaps.constBegin(); it != padMaps.constEnd(); ++it) {
            if (!it.key().contains("#6btn")) continue;
            bool hasSemantic = false;
            for (int v : it.value())
                if (v >= 1000) { hasSemantic = true; break; }
            if (!hasSemantic) drop << it.key();
        }
        for (const QString& k : drop) padMaps.remove(k);
        if (!drop.isEmpty()) padMapsMigrated = true;
    }

    if (!o.contains("pad_maps")
        && (!padProfiles.isEmpty() || !xinputMapping.isEmpty() || !xiScoped.isEmpty())) {
        padProfiles.clear();
        xinputMapping.clear();
        xiScoped.clear();
        padMapsMigrated = true;
    }

    // 범위별 터보
    turboScoped.clear();
    {
        QJsonObject to = o["turbo_scoped"].toObject();
        for (auto it = to.begin(); it != to.end(); ++it)
            turboScoped[it.key()] = it.value().toString();
    }

    // 장치 이름 → 플레이어 배정
    padAssign.clear();
    {
        QJsonObject ao = o["pad_assign"].toObject();
        for (auto it = ao.begin(); it != ao.end(); ++it)
            padAssign[it.key()] = it.value().toInt();
    }

    // 머신 세팅 (DIP/BIOS): { romName: { key: value } }
    auto loadStrMap2 = [&](const QString& key,
                           QHash<QString,QHash<QString,QString>>& dst) {
        dst.clear();
        QJsonObject mv = o[key].toObject();
        for (auto it = mv.begin(); it != mv.end(); ++it) {
            QHash<QString,QString> vars;
            QJsonObject rv = it.value().toObject();
            for (auto jt = rv.begin(); jt != rv.end(); ++jt)
                vars[jt.key()] = jt.value().toString();
            dst[it.key()] = vars;
        }
    };
    loadStrMap2("machine_vars",             machineVars);
    loadStrMap2("machine_vars_by_platform", machineVarsByPlatform);

    // 사운드 모드: { romName(또는 기종): "hifi" }
    auto loadStrMap1 = [&](const QString& key, QHash<QString,QString>& dst) {
        dst.clear();
        QJsonObject so = o[key].toObject();
        for (auto it = so.begin(); it != so.end(); ++it)
            dst[it.key()] = it.value().toString();
    };
    loadStrMap1("bezel_assign",           bezelAssign);
    loadStrMap1("sound_mode_by_game",     soundModeByGame);
    loadStrMap1("sound_mode_by_platform", soundModeByPlatform);

    // 셰이더 파라미터: { 프리셋이름: { 파라미터: 값 } }
    shaderParams.clear();
    {
        const QJsonObject sp = o["shader_params"].toObject();
        for (auto it = sp.begin(); it != sp.end(); ++it) {
            QHash<QString, float> vals;
            const QJsonObject inner = it.value().toObject();
            for (auto j = inner.begin(); j != inner.end(); ++j)
                vals.insert(j.key(), float(j.value().toDouble()));
            if (!vals.isEmpty()) shaderParams.insert(it.key(), vals);
        }
    }

    ensureDataDirs();

    qDebug() << "AppSettings: 로드 완료 -" << p;

    // 마이그레이션 발생 시 변경 사항을 디스크에 즉시 반영
    if (needMigrationSave) {
        qDebug() << "AppSettings: Fill→Fit 마이그레이션, config.json 자동 저장";
        save(p);
    }
}

// ── 저장 ─────────────────────────────────────────────────
void AppSettings::save(const QString& path) const {
    QString p = path.isEmpty() ? defaultConfigPath() : path;

    QJsonObject o;
    o["rom_path"]           = romPath;
    o["preview_path"]       = previewPath;
    o["screenshot_path"]    = screenshotPath;
    o["save_path"]          = savePath;
    o["cheat_path"]         = cheatPath;
    o["record_path"]        = recordPath;

    o["audio_volume"]       = audioVolume;
    o["audio_sample_rate"]  = audioSampleRate;
    o["audio_buffer_ms"]    = audioBufferMs;
    o["sound_mode"]         = soundMode;
    o["bezel_enabled"]      = bezelEnabled;

    o["video_scale_mode"]   = videoScaleMode;
    // 사용자가 명시적으로 Fill을 선택했으면 마이그레이션 방지 플래그 저장
    if (videoScaleMode == "Fill") o["video_scale_explicit"] = true;
    o["video_smooth"]       = videoSmooth;
    o["video_crt_mode"]     = videoCrtMode;
    o["video_crt_intensity"]= videoCrtIntensity;
    o["video_frameskip"]    = videoFrameskip;
    o["video_flash_guard"]    = videoFlashGuard;
    o["video_flash_strength"] = videoFlashStrength;
    o["video_vsync"]        = videoVsync;
    o["video_shader_path"]  = videoShaderPath;

    o["ui_language"]          = uiLanguage;
    o["show_intro"]           = showIntro;
    o["video_renderer"]       = videoRenderer;
    o["netplay_port"]         = netplayPort;
    o["netplay_input_delay"]  = netplayInputDelay;
    o["netplay_relay_url"]    = netplayRelayUrl;
    o["turbo_period"]         = turboPeriod;
    o["turbo_buttons"]      = turboButtons;

    QJsonArray favArr;
    for (const QString& r : favorites) favArr.append(r);
    o["favorites"]          = favArr;
    o["last_game"]          = lastGame;

    o["input_mode"] = inputMode;
    auto saveIntMap = [&](const QString& key, const QHash<int,int>& src) {
        QJsonObject mo;
        for (auto it = src.begin(); it != src.end(); ++it)
            mo[QString::number(it.key())] = it.value();
        o[key] = mo;
    };
    saveIntMap("xinput_mapping",   xinputMapping);
    saveIntMap("winmm_mapping",    winmmMapping);
    saveIntMap("keyboard_mapping", keyboardMapping);
    saveIntMap("keyboard_mapping_six", keyboardMappingSix);

    // 기종별/게임별 컨트롤 매핑
    auto saveScoped = [&](const QString& key,
                          const QHash<QString,QHash<int,int>>& src) {
        QJsonObject so;
        for (auto it = src.begin(); it != src.end(); ++it) {
            QJsonObject mo;
            for (auto jt = it.value().begin(); jt != it.value().end(); ++jt)
                mo[QString::number(jt.key())] = jt.value();
            so[it.key()] = mo;
        }
        o[key] = so;
    };
    saveScoped("kb_scoped", kbScoped);
    saveScoped("xi_scoped", xiScoped);
    saveScoped("wm_scoped", wmScoped);
    saveScoped("pad_profiles", padProfiles);   // (구버전 — 호환용으로만 남김)
    saveScoped("pad_maps",     padMaps);       // 단일 저장소
    {
        QJsonObject ao;
        for (auto it = padAssign.constBegin(); it != padAssign.constEnd(); ++it)
            ao[it.key()] = it.value();
        o["pad_assign"] = ao;
        QJsonObject to;
        for (auto it = turboScoped.constBegin(); it != turboScoped.constEnd(); ++it)
            to[it.key()] = it.value();
        o["turbo_scoped"] = to;
    }

    // 머신 세팅 (DIP/BIOS) — 게임별 + 기종별
    auto saveStrMap2 = [&](const QString& key,
                           const QHash<QString,QHash<QString,QString>>& src) {
        QJsonObject mv;
        for (auto it = src.begin(); it != src.end(); ++it) {
            QJsonObject rv;
            for (auto jt = it.value().begin(); jt != it.value().end(); ++jt)
                rv[jt.key()] = jt.value();
            mv[it.key()] = rv;
        }
        o[key] = mv;
    };
    saveStrMap2("machine_vars",             machineVars);
    saveStrMap2("machine_vars_by_platform", machineVarsByPlatform);

    // 사운드 모드 — 게임별 + 기종별
    auto saveStrMap1 = [&](const QString& key, const QHash<QString,QString>& src) {
        QJsonObject so;
        for (auto it = src.begin(); it != src.end(); ++it)
            so[it.key()] = it.value();
        o[key] = so;
    };
    saveStrMap1("bezel_assign",           bezelAssign);
    saveStrMap1("sound_mode_by_game",     soundModeByGame);
    saveStrMap1("sound_mode_by_platform", soundModeByPlatform);

    {
        QJsonObject sp;
        for (auto it = shaderParams.begin(); it != shaderParams.end(); ++it) {
            QJsonObject inner;
            for (auto j = it.value().begin(); j != it.value().end(); ++j)
                inner[j.key()] = double(j.value());
            if (!inner.isEmpty()) sp[it.key()] = inner;
        }
        o["shader_params"] = sp;
    }

    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "AppSettings: 저장 실패 -" << p;
        return;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    qDebug() << "AppSettings: 저장 완료 -" << p;
}

void AppSettings::resetToDefaults() {
    const QString base = baseDir();
    romPath        = base + "/roms";
    previewPath    = base + "/previews";
    screenshotPath = base + "/screenshots";
    savePath       = base + "/saves";

    audioVolume     = 100;
    audioSampleRate = 48000;
    audioBufferMs   = 80;

    videoScaleMode      = "Fit";
    videoSmooth         = false;
    videoCrtMode        = false;
    videoCrtIntensity   = 0.4;
    videoVsync          = true;
    videoFrameskip      = 0;
    bezelEnabled        = false;

    netplayPort = 7845;

    soundMode = "original";
    soundModeByGame.clear();
    soundModeByPlatform.clear();
}
