// ArcadeLayout_data.h — 6버튼 격투 배치가 적용되는 롬셋 목록 (자동 생성)
//
//  생성 근거: FBNeo 소스(retro_input.cpp)의 bStreetFighterLayout 규칙을
//    드라이버 입력 정의에 그대로 적용해 뽑았다.
//      · P1 에 약/중/강 펀치 3종 + 약/중/강 킥 3종이 모두 있으면 6버튼 배치
//      · 또는 CPS2 하드웨어이면서 P1 사격 버튼이 5개 이상이면 6버튼 배치
//    대상 드라이버: d_cps1 / d_cps2 / d_cps3 / d_sf / d_deco32 /
//                   d_itech32 / d_taotaido / d_segas32 / d_taitof3
//  ※ 손으로 고치지 말 것 — 코어를 갱신하면 다시 뽑는다.
#pragma once

// 정렬된 상태여야 한다 (이진 탐색)
static const char* const kSixButtonRoms[] = {
    "brival", "brivalj", "dankuga", "dstlk", "dstlka", "dstlkb", "dstlkh", "dstlku",
    "dstlku1d", "dstlkur1", "ecofghtr", "ecofghtra", "ecofghtrd", "ecofghtrh",
    "ecofghtru", "ecofghtru1", "fghthist", "fghthista", "fghthistb", "fghthistj",
    "fghthistja", "fghthistjb", "fghthistu", "fghthistua", "fghthistub", "fghthistuc",
    "gblchmp", "hsf2", "hsf230b", "hsf2a", "hsf2app", "hsf2d", "hsf2da", "hsf2df",
    "hsf2ev2", "hsf2j", "hsf2j1", "hsf2j2", "hsf2jpp", "hsf2pp", "kaiserkn",
    "kaiserknj", "msh", "msha", "mshb", "mshbh", "mshbr1", "mshh", "mshj", "mshjr1",
    "mshu", "mshud", "mshvsf", "mshvsfa", "mshvsfa1", "mshvsfb", "mshvsfb1", "mshvsfbh",
    "mshvsfcph", "mshvsfem", "mshvsfh", "mshvsfj", "mshvsfj1", "mshvsfj2", "mshvsfu",
    "mshvsfu1", "mshvsfu1d", "mvsc", "mvsca", "mvscar1", "mvscb", "mvscbh", "mvscem",
    "mvsch", "mvscj", "mvscjr1", "mvscjsing", "mvscr1", "mvscu", "mvscud", "mvscur1",
    "nwarr", "nwarra", "nwarrb", "nwarrh", "nwarru", "nwarrud", "redearth", "redearthn",
    "redearthnr1", "redearthr1", "ringdest", "ringdesta", "ringdestb", "ringdesth",
    "ringdstd", "sf", "sf2", "sf2acc", "sf2acca", "sf2accp2", "sf2amf", "sf2amf10",
    "sf2amf11", "sf2amf12", "sf2amf13", "sf2amf14", "sf2amf2", "sf2amf3", "sf2amf4",
    "sf2amf5", "sf2amf6", "sf2amf7", "sf2amf8", "sf2amf9", "sf2b", "sf2b2", "sf2b3",
    "sf2b4", "sf2b5", "sf2bhh", "sf2ce", "sf2ceb", "sf2ceb2", "sf2ceb3", "sf2ceb4",
    "sf2ceb5", "sf2ceblp", "sf2cebltw", "sf2cebr", "sf2ceda", "sf2ceea", "sf2ceeab2",
    "sf2ceeab3", "sf2ceeabl", "sf2ceec", "sf2ceh", "sf2ceja", "sf2cejab2", "sf2cejabl",
    "sf2cejb", "sf2cejc", "sf2cems6a", "sf2cet", "sf2ceua", "sf2ceuab2", "sf2ceuab3",
    "sf2ceuab4", "sf2ceuab5", "sf2ceuab6", "sf2ceuab7", "sf2ceuab8", "sf2ceuab9",
    "sf2ceuabl", "sf2ceub", "sf2ceuc", "sf2ceucbl", "sf2ceupl", "sf2cre", "sf2dkot2",
    "sf2dongb", "sf2ea", "sf2eb", "sf2ebbl", "sf2ebbl2", "sf2ebbl3", "sf2ebbl4",
    "sf2ed", "sf2ee", "sf2ef", "sf2em", "sf2en", "sf2gm", "sf2hf", "sf2hfj", "sf2hfjb",
    "sf2hfjb2", "sf2hfsce", "sf2hfu", "sf2hfub", "sf2j", "sf2j17", "sf2ja", "sf2jc",
    "sf2jf", "sf2jh", "sf2jl", "sf2jla", "sf2koryu", "sf2koryu2", "sf2koryu3",
    "sf2koryua", "sf2level", "sf2ly", "sf2md", "sf2mdt", "sf2mdta", "sf2mdtb",
    "sf2mdtc", "sf2mega", "sf2mega2", "sf2mix", "sf2mkot", "sf2mkot2", "sf2pp",
    "sf2prime", "sf2qp1", "sf2qp2", "sf2rb", "sf2rb2", "sf2rb3", "sf2rb4", "sf2rb5",
    "sf2rb6", "sf2re", "sf2red", "sf2red2", "sf2reda", "sf2redp2", "sf2rk", "sf2rk2",
    "sf2rules", "sf2sl73a", "sf2stt", "sf2thndr", "sf2thndr2", "sf2tlona", "sf2tlona2",
    "sf2tlonb", "sf2tlonb2", "sf2tlonc", "sf2tlonc2", "sf2ua", "sf2ub", "sf2uc",
    "sf2ud", "sf2ue", "sf2uf", "sf2ug", "sf2uh", "sf2ui", "sf2uk", "sf2um", "sf2v004",
    "sf2v0042", "sf2v0043", "sf2yyc", "sf2yyc2", "sfa", "sfa2", "sfa2u", "sfa2uhc",
    "sfa2ultra", "sfa2ur1", "sfa3", "sfa3b", "sfa3br", "sfa3ce", "sfa3h", "sfa3hr1",
    "sfa3sp2", "sfa3u", "sfa3ud", "sfa3ur1", "sfa3us", "sfa3xl", "sfach", "sfad",
    "sfan", "sfar1", "sfar2", "sfar3", "sfau", "sfaud", "sfiii", "sfiii2", "sfiii2bh",
    "sfiii2h", "sfiii2j", "sfiii2n", "sfiii3", "sfiii3bh", "sfiii3j", "sfiii3jr1",
    "sfiii3n", "sfiii3na", "sfiii3nar1", "sfiii3nr1", "sfiii3r1", "sfiii3th", "sfiii3u",
    "sfiii3ur1", "sfiii3ws", "sfiii4fs", "sfiii4n", "sfiiia", "sfiiibh", "sfiiih",
    "sfiiij", "sfiiin", "sfiiina", "sfiiiu", "sfj", "sfjan", "sfp", "sfpp", "sftm",
    "sftm110", "sftm111", "sftmj112", "sftmj114", "sftmk112", "sfua", "sfw", "sfz2a",
    "sfz2ad", "sfz2adl", "sfz2al", "sfz2alb", "sfz2ald", "sfz2alh", "sfz2alj",
    "sfz2alk", "sfz2alr1", "sfz2b", "sfz2br1", "sfz2h", "sfz2j", "sfz2jd", "sfz2jr1",
    "sfz2k", "sfz2n", "sfz3a", "sfz3ar1", "sfz3j", "sfz3jr1", "sfz3jr2", "sfz3jr2d",
    "sfz3mix", "sfz3mix13", "sfz3te", "sfza", "sfzach", "sfzar1", "sfzb", "sfzbch",
    "sfzbr1", "sfzch", "sfzcha", "sfzchk", "sfzech", "sfzh", "sfzhch", "sfzhr1", "sfzj",
    "sfzjr1", "sfzjr2", "sfzk", "smbomb", "smbombr1", "ssf2", "ssf2a", "ssf2ar1",
    "ssf2d", "ssf2h", "ssf2j", "ssf2jr1", "ssf2jr2", "ssf2r1", "ssf2t", "ssf2ta",
    "ssf2tad", "ssf2tb", "ssf2tba", "ssf2tbd", "ssf2tbh", "ssf2tbj", "ssf2tbj1",
    "ssf2tbr1", "ssf2tbu", "ssf2td", "ssf2tdf", "ssf2th", "ssf2tnl", "ssf2tu",
    "ssf2tur1", "ssf2u", "ssf2ud", "ssf2us2", "ssf2xj", "ssf2xjjs", "ssf2xjr1",
    "ssf2xjr1d", "ssf2xjr1r", "ssf2xjr1trn", "taotaidoa", "uecology", "vampj", "vampja",
    "vampjbh", "vampjr1", "vhunt2", "vhunt2d", "vhunt2r1", "vhuntj", "vhuntjr1",
    "vhuntjr1s", "vhuntjr2", "vsav", "vsav2", "vsav2d", "vsava", "vsavb", "vsavd",
    "vsavh", "vsavj", "vsavu", "warzard", "warzardr1", "xmcota", "xmcotaa", "xmcotaar1",
    "xmcotaar2", "xmcotab", "xmcotabh", "xmcotah", "xmcotahr1", "xmcotaj", "xmcotaj1",
    "xmcotaj2", "xmcotaj3", "xmcotajr", "xmcotar1", "xmcotar1d", "xmcotau", "xmvsf",
    "xmvsfa", "xmvsfar1", "xmvsfar2", "xmvsfar3", "xmvsfb", "xmvsfcph", "xmvsfem",
    "xmvsfh", "xmvsfj", "xmvsfjr1", "xmvsfjr2", "xmvsfjr3", "xmvsfjr4", "xmvsfr1",
    "xmvsfu", "xmvsfu1d", "xmvsfur1", "xmvsfur2",
};
static const int kSixButtonRomCount =
    int(sizeof(kSixButtonRoms) / sizeof(kSixButtonRoms[0]));
