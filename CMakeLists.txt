cmake_minimum_required(VERSION 3.20)
project(FBNeoRageX VERSION 1.8 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ── Static build 감지: -DCMAKE_PREFIX_PATH=.../qt6-static 로 설정 시 자동 활성화
if(CMAKE_PREFIX_PATH MATCHES "qt6-static")
    message(STATUS "Single-EXE static build mode")
    set(STATIC_QT_BUILD ON)
    # .a 라이브러리를 .dll.a 보다 먼저 찾도록 설정
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib .dll.a)
endif()

# ── Qt6 패키지 ────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Core Widgets OpenGL OpenGLWidgets
    Multimedia MultimediaWidgets Network)

qt_standard_project_setup()

# ── 소스 파일 ─────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/AppSettings.cpp
    src/LibretroCore.cpp
    src/BorderPanel.cpp
    src/GameCanvas.cpp
    src/AudioManager.cpp
    src/NetplayManager.cpp
    src/CheatManager.cpp
    src/GamepadManager.cpp
    src/MainWindow.cpp
)

set(HEADERS
    src/libretro.h
    src/AppSettings.h
    src/EmulatorState.h
    src/DrcPid.h
    src/LibretroCore.h
    src/BorderPanel.h
    src/GameCanvas.h
    src/AudioManager.h
    src/NetplayManager.h
    src/CheatManager.h
    src/GamepadManager.h
    src/GameNamesDb.h
    src/MainWindow.h
)

# ── Qt 리소스 (assets → EXE에 내장) ──────────────────────
set(RESOURCES
    resources.qrc
)

# ── Windows 아이콘 리소스 (.rc → .o, 탐색기 아이콘용) ───────
if(WIN32)
    set(WIN_RC app.rc)
    # windres 가 app.rc 내부의 상대 경로(assets/icon.ico)를 소스 루트 기준으로 찾도록 설정
    if(MINGW)
        set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} --include-dir=${CMAKE_SOURCE_DIR}")
    endif()
else()
    set(WIN_RC "")
endif()

# ── 실행 파일 ─────────────────────────────────────────────
if(WIN32)
    # Windows: WIN32 서브시스템 (콘솔창 없음)
    qt_add_executable(FBNeoRageX WIN32 ${SOURCES} ${HEADERS} ${RESOURCES} ${WIN_RC})
else()
    qt_add_executable(FBNeoRageX ${SOURCES} ${HEADERS} ${RESOURCES})
endif()

# ── 링크 ─────────────────────────────────────────────────
target_link_libraries(FBNeoRageX PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Network
)

if(WIN32)
    # winmm — WinMM 조이스틱 API (아케이드 스틱 / DirectInput 폴백)
    # xinput — XInput (동적 로딩이므로 사실 불필요하나 안전을 위해 주석 유지)
    target_link_libraries(FBNeoRageX PRIVATE winmm)
    # target_link_libraries(FBNeoRageX PRIVATE xinput)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(FBNeoRageX PRIVATE dl pthread)
endif()

# ── 인클루드 경로 ─────────────────────────────────────────
target_include_directories(FBNeoRageX PRIVATE src)

# ── 컴파일 옵션 ───────────────────────────────────────────
if(MSVC)
    target_compile_options(FBNeoRageX PRIVATE /W3 /utf-8)
else()
    target_compile_options(FBNeoRageX PRIVATE -Wall -Wextra -O2)
    if(WIN32)
        # MinGW 런타임 DLL 제거
        target_link_options(FBNeoRageX PRIVATE
            -static-libgcc
            -static-libstdc++
        )
        # Static Qt 빌드 시: Qt 플러그인 + Windows 시스템 라이브러리 추가 링크
        if(STATIC_QT_BUILD)
            message(STATUS "Static Qt6 detected — full static link")

            # ── 플러그인 임포트 (static 빌드, 필수만 포함) ─────────
            # 게임 에뮬레이터에 불필요한 TIFF/WebP/SVG 플러그인 제외
            # → libtiff/libjbig/libwebp 의존성 제거
            qt_import_plugins(FBNeoRageX
                INCLUDE
                    Qt6::QWindowsIntegrationPlugin
                    Qt6::QJpegPlugin
                    Qt6::QGifPlugin
                    Qt6::QWindowsMediaPlugin
                    Qt6::QSchannelBackendPlugin
                EXCLUDE
                    Qt6::QTiffPlugin
                    Qt6::QWebpPlugin
                    Qt6::QICNSPlugin
                    Qt6::QTgaPlugin
                    Qt6::QWbmpPlugin
                    Qt6::QSvgPlugin
                    Qt6::QSvgIconPlugin
                EXCLUDE_BY_TYPE tls
            )

            set(UCRT_LIB C:/msys64/ucrt64/lib)

            # ── 전략: Wrap 타겟을 올바른 위치에서 재구성 ──────────────
            # 문제: 직접 target_link_libraries는 Qt6 전이 의존성(Qt6Gui 등)보다
            #       앞에 배치되어, Qt6Gui가 닫힌 그룹의 심볼을 참조하지 못함.
            # 해결: Wrap 타겟의 INTERFACE_LINK_LIBRARIES를 직접 수정하면
            #       Qt6Gui의 INTERFACE 체인에 의해 Qt6Gui 뒤에 올바르게 배치됨.

            # [0] Threads::Threads → libwinpthread.a (정적) 강제
            #     -Wl,-Bdynamic 없이 static winpthread 직접 경로로 대체
            if(TARGET Threads::Threads)
                set_property(TARGET Threads::Threads PROPERTY
                    INTERFACE_LINK_LIBRARIES "${UCRT_LIB}/libwinpthread.a")
            endif()

            # [1] freetype ↔ harfbuzz 순환 의존성:
            #     WrapFreetype에 --start-group 그룹으로 주입 (Qt6Gui 뒤에 배치됨)
            foreach(_ft WrapFreetype::WrapFreetype WrapSystemFreetype::WrapSystemFreetype)
                if(TARGET ${_ft})
                    set_property(TARGET ${_ft} PROPERTY INTERFACE_LINK_LIBRARIES
                        "-Wl,--start-group;${UCRT_LIB}/libfreetype.a;${UCRT_LIB}/libharfbuzz.a;${UCRT_LIB}/libharfbuzz-subset.a;${UCRT_LIB}/libgraphite2.a;${UCRT_LIB}/libbz2.a;${UCRT_LIB}/librpcrt4.a;-Wl,--end-group")
                    set_property(TARGET ${_ft} PROPERTY INTERFACE_LINK_OPTIONS "")
                endif()
            endforeach()
            # WrapHarfbuzz: WrapFreetype 그룹 안에 포함되므로 비움
            foreach(_hb WrapHarfbuzz::WrapHarfbuzz WrapSystemHarfbuzz::WrapSystemHarfbuzz)
                if(TARGET ${_hb})
                    set_property(TARGET ${_hb} PROPERTY INTERFACE_LINK_LIBRARIES "")
                    set_property(TARGET ${_hb} PROPERTY INTERFACE_LINK_OPTIONS "")
                endif()
            endforeach()

            # [2] WrapOpenSSL: ssl/crypto 뒤에 Windows 네트워크 라이브러리 추가
            if(TARGET WrapOpenSSL::WrapOpenSSL)
                set_property(TARGET WrapOpenSSL::WrapOpenSSL PROPERTY INTERFACE_LINK_LIBRARIES
                    "${UCRT_LIB}/libssl.a;${UCRT_LIB}/libcrypto.a;${UCRT_LIB}/libws2_32.a;${UCRT_LIB}/libwsock32.a;${UCRT_LIB}/libcrypt32.a;${UCRT_LIB}/libsecur32.a")
                set_property(TARGET WrapOpenSSL::WrapOpenSSL PROPERTY INTERFACE_LINK_OPTIONS "")
            endif()

            # [3] 사용하지 않는 WebP/TIFF 비움 (플러그인에서 제외됨)
            foreach(_unused WrapWebP::WrapWebP TIFF::TIFF)
                if(TARGET ${_unused})
                    set_property(TARGET ${_unused} PROPERTY INTERFACE_LINK_LIBRARIES "")
                    set_property(TARGET ${_unused} PROPERTY INTERFACE_LINK_OPTIONS "")
                endif()
            endforeach()
        endif()
    endif()
endif()

# ── 리소스 복사 (빌드 후) ────────────────────────────────
add_custom_command(TARGET FBNeoRageX POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:FBNeoRageX>/assets"
    COMMENT "Copying assets..."
)

# ── 설치 ─────────────────────────────────────────────────
install(TARGETS FBNeoRageX DESTINATION .)
install(DIRECTORY assets DESTINATION .)
