from host_tools import CC, CXX, SHARED_EXPORT, unload_library
"""Compile the production parser; exercise requests, ambiguity and negation."""
import ctypes
import _ctypes
import subprocess
import tempfile
from pathlib import Path
root = Path(__file__).resolve().parent
board = root/'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as folder:
    dll=Path(folder)/'voice.dll'
    subprocess.run([CC,'-shared','-std=c11','-Wall','-Wextra','-Werror',
                    str(board/'nabo_voice_commands.c'),'-o',str(dll)],check=True)
    lib=ctypes.CDLL(str(dll))
    parse=lib.nabo_voice_launch_app
    parse.argtypes=[ctypes.c_char_p];parse.restype=ctypes.c_char_p
    aliases={'网络电台':'radio','电台':'radio','收音机':'radio',
             '设置':'settings','更多设置':'settings','工具':'tools'}
    count=0
    for spoken,app in aliases.items():
        for template in ['打开{}。','请打开{}','小智，请你帮我打开{}吧！','我想玩{}','开启一下{}程序', ' 打开 {} 一下 ']:
            command=template.format(spoken)
            assert parse(command.encode())==app.encode(),command
            count+=1
    for command in ['', '电台','不要打开电台','别打开收音机','我不想打开电台','如何打开电台',
                    '教我怎么打开收音机','打开牌桌','打开电台有什么用','打开电台和设置','打开电台然后关机',
                    '打开未知程序','打开电脑设置','我听过电台这个词','打开跳一跳','打开深潜','打开节奏回声','打开'+'电台'*100]:
        assert parse(command.encode()) is None,command
        count+=1
    assert parse(None) is None
    unload_library(lib._handle)
print(f'PASS: {count+1} production speech parser cases; negation/questions/ambiguity rejected')
