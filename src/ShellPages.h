#pragma once
// ShellPages.h — OPTIONS 메뉴 페이지 팩토리
//
//  페이지 클래스는 각자의 .cpp 안에 숨기고, 밖으로는 만드는 함수만 연다.
//  MainWindow 가 페이지 내부 구현을 알 필요가 없고, 페이지를 고쳐도
//  MainWindow 를 다시 컴파일하지 않는다.

#include <memory>

#include "ShellPage.h"

std::unique_ptr<ShellPage> makeControlsPage  (const ShellHost& host);
std::unique_ptr<ShellPage> makeDirectoriesPage(const ShellHost& host);
std::unique_ptr<ShellPage> makeVideoPage  (const ShellHost& host);
std::unique_ptr<ShellPage> makeAudioPage  (const ShellHost& host);
std::unique_ptr<ShellPage> makeMachinePage(const ShellHost& host);
std::unique_ptr<ShellPage> makeCheatsPage (const ShellHost& host);
std::unique_ptr<ShellPage> makeSystemPage (const ShellHost& host);
std::unique_ptr<ShellPage> makeShotsPage  (const ShellHost& host);
std::unique_ptr<ShellPage> makeNetplayPage(const ShellHost& host);
