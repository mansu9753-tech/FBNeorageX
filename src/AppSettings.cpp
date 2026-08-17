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
    audioDrcMax    = jval(o, "audio_drc_max",        audioDrcMax);

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

    region            = jval(o, "region",               region);
    uiLanguage        = jval(o, "ui_language",          uiLanguage);
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
    loadScoped("pad_profiles", padProfiles);   // 장치 이름별 패드 매핑

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

    // 핫키: { action: encodedInt }
    hotkeyMap.clear();
    QJsonObject hk = o["hotkey_map"].toObject();
    for (auto it = hk.begin(); it != hk.end(); ++it)
        hotkeyMap[it.key()] = it.value().toInt();

    // 경로 디렉터리 자동 생성
    for (const QString& dir : {romPath, previewPath, screenshotPath, savePath, cheatPath, recordPath})
        QDir().mkpath(dir);

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
    o["audio_drc_max"]      = audioDrcMax;

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

    o["region"]               = region;
    o["ui_language"]          = uiLanguage;
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
    saveScoped("pad_profiles", padProfiles);   // 장치 이름별 패드 매핑
    {
        QJsonObject ao;
        for (auto it = padAssign.constBegin(); it != padAssign.constEnd(); ++it)
            ao[it.key()] = it.value();
        o["pad_assign"] = ao;
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

    // 핫키
    QJsonObject hk;
    for (auto it = hotkeyMap.begin(); it != hotkeyMap.end(); ++it)
        hk[it.key()] = it.value();
    o["hotkey_map"] = hk;

    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "AppSettings: 저장 실패 -" << p;
        return;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    qDebug() << "AppSettings: 저장 완료 -" << p;
}
