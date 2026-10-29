# 아바타 이미지 생성기 / Avatar generator (Google Colab)

**KO**
1. https://colab.research.google.com → 파일 → 노트 업로드 → `avatar_generator.ipynb`
2. 런타임 → 런타임 유형 변경 → T4 GPU
3. 셀을 위에서부터 실행 (2번 셀에서 스타일·부품 목록 수정)
4. 내려받은 `avatars.zip` 을 풀어 `assets/avatars/` 에 넣기 (256×320 투명 PNG + catalog.json)

**EN**
1. Open the notebook in Google Colab, set runtime to T4 GPU.
2. Run cells top to bottom (edit style / part lists in cell 2).
3. Unzip the downloaded `avatars.zip` into `assets/avatars/`.

Not tested on Colab by the author; results vary per seed / prompt.
