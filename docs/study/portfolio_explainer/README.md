# 포트폴리오 페이지별 상세 해설

원본 36쪽마다 그림 구성 이유, 요소별 의미, 발표 예시, 코드 근거, 예상 질문과 한계를 설명한다. 표지·목차·보충 설명을 포함해 86쪽이다.

## 재생성

Python에 reportlab, pypdf, Pillow가 필요하며 Poppler를 사용한다. 현재 PC의 번들 Python으로 실행한다.

```powershell
python docs/study/portfolio_explainer/build.py --root D:/work/Josun_Server/git_ProJect --portfolio D:/work/Josun_Server/Thema/A/민수홍_Cpp_멀티스레드_포트폴리오.pdf --content D:/work/Josun_Server/Portfolio/content/documents.json --theme D:/work/Josun_Server/Thema/A/theme.json --output output/pdf/민수홍_멀티스레드_포트폴리오_페이지별_상세해설.pdf --assets tmp/pdfs/portfolio-explainer-assets
```

- build.py: 원본 렌더링, 페이지 구성, 목차·링크, 코드 인용 및 검증 manifest.
- content.py: 원본 36쪽의 해설.
- appendices.py: 객체 수명·잠금·큐·전투·지표·발표·용어 보충 해설.
- 공식 생성 소스 복사본: D:/work/Josun_Server/Portfolio/source/portfolio_explainer.
- 검증 기록: docs/results/2026-10-21/portfolio-explainer.

원본 수정 후 재생성 시 원본 페이지 수와 페이지별 설명이 여전히 일치하는지 검토한다. 성능 수치는 기존 결과에서 인용하며 새 측정값으로 표현하지 않는다.
