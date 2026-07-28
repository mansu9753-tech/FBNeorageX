# 대상 파일이 없을 때만 복사한다.
# 사용자가 편집하는 설정 파일(names.txt 등)을 빌드가 덮어쓰지 않도록 하기 위함.
#   cmake -DSRC=<원본> -DDST=<대상> -P CopyIfMissing.cmake
if(NOT EXISTS "${DST}")
    configure_file("${SRC}" "${DST}" COPYONLY)
    message(STATUS "CopyIfMissing: ${DST} 생성")
endif()
