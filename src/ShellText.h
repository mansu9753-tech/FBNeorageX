#pragma once
// ShellText.h — 셸 메뉴 문구의 언어 변환
//
//  korean 이 true 이면 사전에 있는 영문 문구를 한국어로 바꿔 돌려주고,
//  없으면 원문 그대로 돌려준다. false 이면 항상 원문이다.

#include <QString>

QString shellText(const QString& s, bool korean);
