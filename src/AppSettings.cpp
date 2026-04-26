// AppSettings.cpp — 설정 로드/저장 (JSON config.json)

#include "AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
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

AppSettings::AppSettings() {
#ifdef _WIN32
    // Windows: 실행파일 옆 디렉터리 (포터블 방식)
    QString base = QCoreApplication::applicationDirPath();
    romPath        = base + "/roms";
    previewPath    = base + "/previews";
    screenshotPath = base + "/screenshots";
    savePath       = base + "/saves";
    cheatPath      = base + "/cheats";
    recordPath     = base + "/recordings";
#else
    // Linux / Steam Deck: XDG 표준 경로 사용
    // config.json, saves, cheats → ~/.local/share/FBNeoRageX/
    // screenshots  → ~/Pictures/FBNeoRageX
    // recordings   → ~/Videos/FBNeoRageX
    // ROMs         → ~/ROMs/fbneo (Steam Deck 관행)
    QString data   = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QString pics   = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QString vids   = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    QString home   = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);

    romPath        = home   + "/ROMs/fbneo";
    previewPath    = data   + "/previews";
    screenshotPath = pics   + "/FBNeoRageX";
    savePath       = data   + "/saves";
    cheatPath      = data   + "/cheats";
    recordPath     = vids   + "/FBNeoRageX";
#endif
}

QString AppSettings::defaultConfigPath() const {
#ifdef _WIN32
    return QCoreApplication::applicationDirPath() + "/config.json";
#else
    // Linux: ~/.local/share/FBNeoRageX/config.json
    QString data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(data);
    return data + "/config.json";
#endif
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
    videoVsync      = jval(o, "video_vsync",         videoVsync);
    videoShaderPath = jval(o, "video_shader_path",  videoShaderPath);

    region          = jval(o, "region",              region);
    netplayPort     = jval(o, "netplay_port",        netplayPort);
    turboPeriod     = jval(o, "turbo_period",        turboPeriod);
    turboButtons    = jval(o, "turbo_buttons",       turboButtons);

    // 즐겨찾기 (JSON 배열)
    favorites.clear();
    for (const QJsonValue& v : o["favorites"].toArray())
        favorites.append(v.toString());

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
    o["video_vsync"]        = videoVsync;
    o["video_shader_path"]  = videoShaderPath;

    o["region"]             = region;
    o["netplay_port"]       = netplayPort;
    o["turbo_period"]       = turboPeriod;
    o["turbo_buttons"]      = turboButtons;

    QJsonArray favArr;
    for (const QString& r : favorites) favArr.append(r);
    o["favorites"]          = favArr;

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

    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "AppSettings: 저장 실패 -" << p;
        return;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    qDebug() << "AppSettings: 저장 완료 -" << p;
}
