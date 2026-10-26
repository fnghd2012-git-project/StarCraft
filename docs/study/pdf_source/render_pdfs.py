'최종 PDF 전 페이지를 Poppler로 렌더한다. 검수 이미지는 tmp/pdfs 안에만 둔다.'
from pathlib import Path
import concurrent.futures,json,subprocess
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'output/pdf';REVIEW=ROOT/'tmp/pdfs/iocp-review'
POPPLER=Path.home()/'.cache/codex-runtimes/codex-primary-runtime/dependencies/native/poppler/Library/bin/pdftoppm.exe'
REVIEW.mkdir(parents=True,exist_ok=True)
manifest=json.loads((OUT/'build-manifest.json').read_text('utf-8'))
def render(item):
    path=OUT/item['file']
    subprocess.run([POPPLER,'-png','-r','100',path,REVIEW/path.stem],check=True,creationflags=subprocess.CREATE_NO_WINDOW)
    return item['file']
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
    for name in executor.map(render,manifest['pdfs']):print('[OK] Rendered',name,flush=True)
