"""Exercise/render the actual firmware FM scene using stubbed hardware only."""
import os
import re
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
# Exercise the real queued settings adjustment; hardware/NVS remain boundaries.
source=(BOARD/'nabo_apps.cc').read_text('utf-8')
adjust=re.search(r'int nabo_apps_adjust_volume\(int direction\) \{.*?\n\}',source,re.S)
assert adjust, 'production volume entry point is missing'
volume_source=out/'volume_settings.cc'
volume_source.write_text('#include <algorithm>\n#include <atomic>\n'
                        'std::atomic<int> volume{70}, setting_command{-1};\n'
                        'enum { ST_VOLUME = 1 };\n'+adjust.group()+'\n','utf-8')
subprocess.run([CXX,'-std=c++20','-O2','-Wall','-Wextra','-Werror','-DNRF_HOST_PREVIEW','-I',str(ROOT/'tests/radio_host'),'-I',str(BOARD),
                str(BOARD/'nabo_radio_view.cc'),str(ROOT/'tests/radio_host/preview.cc'),str(volume_source),*objects,'-o',str(exe)],check=True)
subprocess.run([str(exe),str(BOARD/'radio_assets'),str(out)],check=True)
