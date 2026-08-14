#pragma once
// GameHardware.h — 게임 목록 탭용 기종(하드웨어) 분류
//
// 롬 이름 접두사 기반 휴리스틱이다. FBNeo 는 롬셋 이름만으로 기판을 알려주지
// 않으므로, 잘 알려진 롬셋 이름으로 분류한다. 분류되지 않은 게임은 ETC 로 간다.
//
// ★ MainWindow::gamePlatform() 과는 별개의 함수다.
//   그쪽은 "기종별 컨트롤·머신세팅 저장" 의 키로 쓰이므로, 값을 바꾸면 사용자가
//   이미 저장해 둔 설정이 무효가 된다. 그래서 건드리지 않고 여기서 따로 세분화한다.

#include <QString>
#include <QVector>
#include <QHash>
#include <algorithm>

struct GameHardwareDef {
    const char* id;      // 내부 키
    const char* label;   // 탭에 표시할 이름
};

// 탭 표시 순서 — 크게 NEOGEO / CPS / ETC 세 그룹으로만 묶는다.
//   (기종을 잘게 나누니 버튼이 너무 많아져 목록 화면이 지저분해졌다)
//   내부 분류는 세밀하게 유지하고, 탭에서만 묶어서 보여준다.
inline const QVector<GameHardwareDef>& gameHardwareList() {
    static const QVector<GameHardwareDef> v = {
        { "neogeo", "NEOGEO" },
        { "cps",    "CPS"    },   // CPS1 + CPS2 + CPS3
        { "etc",    "ETC"    },   // 그 외 전부
    };
    return v;
}

// 세부 기종 id → 탭 그룹 id
inline QString gameHardwareGroup(const QString& id) {
    if (id == QLatin1String("neogeo")) return QStringLiteral("neogeo");
    if (id == QLatin1String("cps1") || id == QLatin1String("cps2")
        || id == QLatin1String("cps3")) return QStringLiteral("cps");
    return QStringLiteral("etc");
}

// ── 접두사 → 기종 테이블 ─────────────────────────────────────────
//   긴 접두사부터 검사한다. (예: sfiii > sfa > sf2 순으로 걸러야
//    "sf" 계열이 서로 잡아먹지 않는다)
inline const QVector<QPair<QString, QString>>& gameHardwarePrefixes() {
    static QVector<QPair<QString, QString>> tbl;
    if (!tbl.isEmpty()) return tbl;

    auto add = [](const char* hw, std::initializer_list<const char*> pfx) {
        for (const char* p : pfx) tbl.append({ QString::fromLatin1(p), QString::fromLatin1(hw) });
    };

    // ── NEOGEO (MVS/AES) ─────────────────────────────────────
    add("neogeo", {
        "kof","mslug","garou","samsho","rbff","fatfury","aof","wh1","wh2","whp",
        "nam1975","lbowling","blazstar","lastsold","neo","magdrop","neobombe",
        "turfmast","lastblad","rotd","ssideki","twinspri","ironclad","matrim",
        "svc","kizuna","shocktro","ragnagrd","breakers","galaxyfg","wjammers",
        "pbobbln","pbobblen","pbobbl2n","puzzledp","puzzldpr","goalx3","kabukikl",
        "sengoku","spinmast","strhoop","superspy","trally","viewpoin","wakuwak7",
        "androdun","aodk","bakatono","bstars","burningf","crswords","cyberlip",
        "eightman","fbfrenzy","flipshot","froman2b","fswords","ganryu","gowcaizr",
        "gpilots","janshin","joyjoy","karnovr","kotm","legendos","maglord","mahretsu",
        "marukodq","miexchng","minasan","moshougi","mutnat","ncombat","ncommand",
        "ninjamas","nitd","overtop","panicbom","pgoal","pnyaa","popbounc","preisle2",
        "pspikes2","pulstar","quizdai2","quizdais","quizkof","ridhero","roboarmy",
        "savagere","sdodgeb","shocktr2","socbrawl","sonicwi","stakwin","strhoop",
        "tophuntr","tpgolf","trally","tws96","vliner","zedblade","zintrick",
        "3countb","2020bb","alpham2","b2b","bangbead","bjourney","blazstar","crsword",
    });

    // ── CPS3 ─────────────────────────────────────────────────
    add("cps3", { "sfiii","jojo","redearth","warzard" });

    // ── CPS2 ─────────────────────────────────────────────────
    add("cps2", {
        "19xx","1944","armwar","avsp","batcir","choko","csclub","cybots","ddsom",
        "ddtod","dimahoo","dstlk","ecofghtr","gigawing","hsf2","jyangoku","megaman2",
        "mmancp2","mmatrix","mpang","msh","mshvsf","mvsc","nwarr","progear","pzloop2",
        "qndream","ringdest","sfa","sfz","sgemf","spf2t","ssf2","vampj","vhunt",
        "vsav","xmcota","xmvsf","gwingj","mbomber","pgear",
    });

    // ── CPS1 ─────────────────────────────────────────────────
    add("cps1", {
        "1941","3wonders","captcomm","cawing","chikij","cworld2j","dino","dynwar",
        "ffight","forgottn","ganbare","ghouls","kod","knights","megaman","rockman",
        "mercs","msword","mtwins","nemo","pnickj","punisher","qad","qtono2","sf2",
        "slammast","strider","unsquad","varth","willow","wof","wonder3","area88",
        "pang3","kodj","sfach","daimakai","mbombrd","perfect","stridrj",
    });

    // ── CAPCOM (CPS 이전 기판) ───────────────────────────────
    add("capcom", {
        "1942","1943","commando","gunsmoke","sectionz","trojan","vulgus","sonson",
        "exedexes","gng","higemaru","lwings","avengers","blktiger","tigeroad",
        "sidearms","bionicc","srumbler","legendos","hitmvsc","pirates","dokaben",
        "makaimur","gngt","diamond","gulunpa","lastduel","madgear","blkdrgon",
    });

    // ── SNK (네오지오 이전) ───────────────────────────────────
    add("snk", {
        "ikari","tnk3","athena","victroad","gwar","bermudat","psychos","chopper",
        "aso","marvins","jcross","sathena","prehisle","streetsm","searchar",
        "fitegolf","tdfever","countryc","alphamis","timesold","skyadvnt","gangwars",
        "bbusters","mechatt","dogosoke","fsoccer","hal21","munchmo","paddlem",
        "tocki","worldwar","canvas",
    });

    // ── IREM ─────────────────────────────────────────────────
    add("irem", {
        "rtype","hharry","dkgen","poundfor","airduel","gallop","cosmccop","kengo",
        "matchit","xmultipl","dbreed","loht","imgfight","nspirit","mrheli","bchopper",
        "gunforce","bmaster","lethalth","thndblst","uccops","mysticri","gunhohki",
        "majtitl","hook","ppan","inthunt","kaiteids","leaguemn","ssoldier","psoldier",
        "dsoccr","gunforc2","geostorm","nbbatman","hcube","spelunk","kungfum","ldrun",
        "kidniki","vigilant","lotlot","travrusa","battroad","horizon","youjyudn",
        "quizf1","riskchal","shisen","dbreedm72",
    });

    // ── CAVE ─────────────────────────────────────────────────
    add("cave", {
        "ddonpach","donpachi","esprade","guwange","dfeveron","uopoko","ddpdoj",
        "espgal","mushi","ketsui","pinkswts","deathsml","ibara","ddp","feversos",
        "gaia","hotdogst","korokoro","plegends","sailormn","agallet","metmqstr",
        "pwrinst","theroes","tjumpman","dodonpachi",
    });

    // ── TOAPLAN ──────────────────────────────────────────────
    add("toaplan", {
        "batsugun","dogyuun","hellfire","truxton","tatsujin","zerowing","outzone",
        "snowbros","fixeight","vfive","grindstm","kingdmgp","kbash","pipibibs",
        "whoopee","tekipaki","ghox","dharma","rallybik","demonwld","vimana","teki",
        "samesame","fireshrk","bgaregga","batrider","bbakraid","sstriker","mahoudai",
        "shippumd","kyustrkr",
    });

    // ── PSIKYO ───────────────────────────────────────────────
    add("psikyo", {
        "gunbird","strikers","s1945","samuraia","btlkroad","sengokmj","tengai",
        "gachiko","loverboy","daraku","hotgmck","sngkace","dragnblz","gnbarich",
        "mjgtaste","soldivid","tgm2",
    });

    // ── SEGA ─────────────────────────────────────────────────
    add("sega", {
        "altbeast","shinobi","goldnaxe","aliensyn","wb3","tturf","aburner","outrun",
        "hangon","spacehrr","eswat","mwalk","astorm","riotcity","sonicbom","dcclub",
        "passsht","bloxeed","cotton","aurail","bayroute","cltchitr","ddcrew","dunkshot",
        "exctleag","fantzone","fpoint","hwchamp","lghost","loffire","mjleague","quartet",
        "ryukyu","sdi","sjryuko","suprleag","tetris","timescan","toutrun","wrestwar",
        "cyberpol","dbzvrvs","pontoon","segasyse","opaopa","transfrm",
    });

    // ── KONAMI ───────────────────────────────────────────────
    add("konami", {
        "tmnt","ssriders","xmen","simpsons","aliens","sunsetbl","vendetta","parodius",
        "gradius","salamand","nemesis","mia","blockhl","punkshot","thunderx","ajax",
        "contra","gbusters","hcrash","rollerg","bucky","lgtnfght","mystwarr","violent",
        "metamrph","gaiapols","dbz","wecleman","asterix","batman","crimfght","cuebrick",
        "detatwin","overdriv","rockrage","spy","surpratk","tgtpanic","tp84","twin16",
        "vulcan","hexion","mainevt","bottom9","blswhstl","moo","prmrsocr","qgakumon",
        "run","sbasketb","scotrsht","shaolins","trigon","typhoon","ultraman",
    });

    // ── TAITO ────────────────────────────────────────────────
    add("taito", {
        "rastan","bublbobl","darius","ninjaw","opwolf","tokio","kabukiz","cadash",
        "dondokod","growl","gunfront","liquidk","metalb","ninjak","pulirula","qzshowby",
        "superman","thundfox","undrfire","warriorb","footchmp","hthero","koshien",
        "mjnquest","pbobble","bonzeadv","asuka","bshark","chasehq","contcirc","dinorex",
        "elvactr","exzisus","finalb","galastrm","gigandes","gunbustr","jumping","kaiserkn",
        "lkage","masterw","mizubaku","nastar","othunder","plotting","rambo","rbisland",
        "rgum","runark","sagaia","slapshot","sonicbom","spacegun","tetrist","topspeed",
        "twinhawk","volfied","wgp","wardner","arkanoid","dblaxle","drtoppel","earthjkr",
        "fhawk","flstory","gladiatr","insectx","matchit2","minivadr","msbingo","onna34ro",
        "opwolf3","qcrayon","raimais","recordbr","solfigtr","syvalion","tnzs","tubeit",
    });

    // ── DATA EAST ────────────────────────────────────────────
    add("dataeast", {
        "karnov","robocop","hbarrel","midres","dassault","cninja","rohga","funkyjet",
        "boogwing","tumblep","joemac","wizdfire","dietgo","sshangha","edrandy","cbuster",
        "darkseal","supbtime","actfancr","astyanax","backfirw","bloodwar","chelnov",
        "cntsteer","dblewing","dec0","dec8","ffantasy","gondo","hippodrm","lastmisn",
        "liberate","mutantf","nitrobal","oscar","pcktgal","secretag","shootout","sidepckt",
        "spool3","stadhero","stoneage","thndzone","vaportra","captaven","fghthist",
        "lockload","nslasher","schmeisr","sotsugyo","stfight",
    });

    // ── NAMCO ────────────────────────────────────────────────
    add("namco", {
        "pacman","galaga","digdug","xevious","mappy","drgnbstr","rollingt","assault",
        "cosmogng","finallap","marvland","ordyne","phelios","rthunder","sgunner",
        "numanath","quester","valkyrie","bosco","dragonbu","galaga88","grobda","kaitei",
        "libble","mmaze","motos","namcona","pacland","skykid","splatter","superpac",
        "toypop","wldcourt","berabohm","blastoff","burnforc","dirtfoxj","fourtrax",
        "hopmappy","kyukaidk","metlhawk","pistoldm","shadowld","suzuka","tinklpit",
        "tceptor","wstadium",
    });

    // 긴 접두사 우선 (sfiii > sfa > sf2 처럼 서로 잡아먹지 않게)
    std::stable_sort(tbl.begin(), tbl.end(),
        [](const QPair<QString,QString>& a, const QPair<QString,QString>& b){
            return a.first.size() > b.first.size();
        });
    return tbl;
}

// 롬 이름 → 기종 id. 모르면 "etc".
inline QString gameHardwareOf(const QString& rom) {
    static QHash<QString, QString> cache;
    auto it = cache.constFind(rom);
    if (it != cache.constEnd()) return it.value();

    const QString lc = rom.toLower();
    QString hw = QStringLiteral("etc");
    for (const auto& [pfx, id] : gameHardwarePrefixes()) {
        if (lc.startsWith(pfx)) { hw = id; break; }
    }
    cache.insert(rom, hw);
    return hw;
}

// 기종 id → 표시 이름
inline QString gameHardwareLabel(const QString& id) {
    for (const auto& d : gameHardwareList())
        if (id == QLatin1String(d.id)) return QString::fromLatin1(d.label);
    return id.toUpper();
}
