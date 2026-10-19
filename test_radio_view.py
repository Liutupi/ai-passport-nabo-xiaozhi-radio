"""Exercise/render the actual firmware FM scene using stubbed hardware only."""
import os
from pathlib import Path
import subprocess
from host_tools import CC, CXX
ROOT=Path(__file__).resolve().parent
BOARD=ROOT/'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
out=ROOT/'private/fm-preview'
out.mkdir(parents=True,exist_ok=True)
objects=[]
for module in ['catalog','gfx','keys','sprites','clock']:
    obj=out/(module+'.o')
    subprocess.run([CC,'-std=c11','-O2','-Wall','-Wextra','-Werror','-DNRF_CLOCK_HOST','-c',str(BOARD/f'nabo_radio_{module}.c'),'-o',str(obj)],check=True)
    objects.append(str(obj))
exe=out/('preview.exe' if os.name=='nt' else 'preview')
subprocess.run([CXX,'-std=c++20','-O2','-Wall','-Wextra','-Werror','-DNRF_HOST_PREVIEW','-I',str(ROOT/'tests/radio_host'),'-I',str(BOARD),
                str(BOARD/'nabo_radio_view.cc'),str(ROOT/'tests/radio_host/preview.cc'),*objects,'-o',str(exe)],check=True)
subprocess.run([str(exe),str(BOARD/'radio_assets'),str(out)],check=True)
