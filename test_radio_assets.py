"""Production NFM1 decoder, NFC1 catalog and raw-key timing regression checks."""
import ctypes as C
import importlib.util
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile
from host_tools import CC, SHARED_EXPORT, unload_library

ROOT=Path(__file__).resolve().parent
BOARD=ROOT/'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
ASSETS=BOARD/'radio_assets'
class Sprite(C.Structure):
    _fields_=[('data',C.c_void_p),('size',C.c_size_t),('pixels',C.c_void_p),('capacity',C.c_size_t)] + [(n,C.c_uint) for n in ['count','action','width','height','fps','frames','frame','palette','offsets']]+[('loop',C.c_bool),('valid',C.c_bool)]
class Catalog(C.Structure):
    _fields_=[('data',C.c_void_p),('size',C.c_size_t),('count',C.c_uint),('regions',C.c_uint),('strings',C.c_uint)]
class Station(C.Structure):
    _fields_=[('uid',C.c_uint32),('heat',C.c_uint32),('qt',C.c_uint32)]+[(n,C.c_char_p) for n in ['name','url','py','region']]+[(n,C.c_uint) for n in ['region_id','codec','types']]
def decode_row(blob,at,width,ref):
    row=bytearray(ref or bytes(width));x=0
    while True:
        c=blob[at];at+=1
        if c==192:break
        n=(c&63)+1;kind=c>>6
        if kind==1:row[x:x+n]=blob[at:at+n];at+=n
        if kind==2:row[x:x+n]=bytes([blob[at]])*n;at+=1
        x+=n
    return row

with tempfile.TemporaryDirectory() as temp:
    dll=Path(temp)/'fm.dll'
    sources=['nabo_radio_sprites.c','nabo_radio_catalog.c','nabo_radio_keys.c','nabo_radio_gfx.c']
    subprocess.run([CC,'-shared','-std=c11','-O2','-Wall','-Wextra','-Werror',SHARED_EXPORT,
                    *[str(BOARD/s) for s in sources],'-o',str(dll)],check=True)
    lib=C.CDLL(str(dll))
    for f in ['nrf_sprite_init','nrf_sprite_select','nrf_sprite_frame','nrf_catalog_init','nrf_catalog_get','nrf_catalog_url','nrf_catalog_match']:
        getattr(lib,f).restype=C.c_bool
    lib.nrf_sprite_init.argtypes=[C.POINTER(Sprite),C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t]
    lib.nrf_sprite_select.argtypes=[C.POINTER(Sprite),C.c_char_p]
    lib.nrf_sprite_frame.argtypes=[C.POINTER(Sprite),C.c_uint]
    blob=(ASSETS/'radio_sprites.bin').read_bytes();buf=C.create_string_buffer(blob);pixels=(C.c_uint8*(158*139))();s=Sprite()
    assert lib.nrf_sprite_init(C.byref(s),buf,len(blob),pixels,len(pixels))
    checks=frames=0
    for action in range(s.count):
        at=8+action*32;name=blob[at:at+12].rstrip(b'\0');w,h,fps,loop,nf,pal,offsets,_=struct.unpack_from('<HHBBHIII',blob,at+12)
        assert lib.nrf_sprite_select(C.byref(s),name)
        ref=None;gold=[]
        for frame in range(nf):
            pos=struct.unpack_from('<I',blob,offsets+4*frame)[0]
            image=b''.join(decode_row(blob,pos+struct.unpack_from('<H',blob,pos+2*y)[0],w,None if ref is None else ref[y*w:(y+1)*w]) for y in range(h))
            gold.append(image);ref=image
            assert lib.nrf_sprite_frame(C.byref(s),frame)
            assert bytes(pixels)==image,(name,frame)
            frames+=1
        for frame in [0,nf-1,1,0,nf//2]:
            assert lib.nrf_sprite_frame(C.byref(s),frame)
            assert bytes(pixels)==gold[frame],(name,frame,'seek/loop')
        checks+=1
    assert not lib.nrf_sprite_select(C.byref(s),b'unknown')
    # Each action rejects truncated rows, impossible dimensions, palette overflow.
    rng=random.Random(42)
    for _ in range(200):
        bad=bytearray(blob);off=rng.randrange(8,8+32*13);bad[off]=255
        b=C.create_string_buffer(bytes(bad));sp=Sprite()
        if lib.nrf_sprite_init(C.byref(sp),b,len(bad),pixels,len(pixels)):
            lib.nrf_sprite_select(C.byref(sp),b'idle_off')
    lib.nrf_catalog_init.argtypes=[C.POINTER(Catalog),C.c_void_p,C.c_size_t]
    lib.nrf_catalog_get.argtypes=[C.POINTER(Catalog),C.c_int,C.POINTER(Station)]
    lib.nrf_catalog_match.argtypes=[C.POINTER(Station),C.c_char_p]
    cat_blob=(ASSETS/'radio_catalog.bin').read_bytes();cb=C.create_string_buffer(cat_blob);cat=Catalog()
    assert lib.nrf_catalog_init(C.byref(cat),cb,len(cat_blob));assert cat.count==1105
    uids=set();guangzhou=[]
    for i in range(cat.count):
        station=Station();assert lib.nrf_catalog_get(C.byref(cat),i,C.byref(station));uids.add(station.uid)
        url=C.create_string_buffer(384);assert lib.nrf_catalog_url(C.byref(station),url,len(url));assert url.value.startswith((b'http://',b'https://'))
        if lib.nrf_catalog_match(C.byref(station),b'GZJT'):guangzhou.append(station.name.decode())
    assert len(uids)==cat.count
    assert any('广州' in name and '交通' in name for name in guangzhou),guangzhou
    assert not lib.nrf_catalog_get(C.byref(cat),-1,C.byref(station))
    for n in [0,3,11,len(cat_blob)-1]:
        truncated=Catalog();assert not lib.nrf_catalog_init(C.byref(truncated),cb,n)
    CALLBACK=C.CFUNCTYPE(None,C.c_void_p,C.c_int,C.c_int)
    events=[]
    @CALLBACK
    def emit(ctx,key,event):events.append((key,event))
    keys=C.create_string_buffer(256)
    lib.nrf_keys_init(keys,emit,None)
    def edge(key,down,t):lib.nrf_keys_edge(keys,key,down,C.c_uint32(t))
    def tick(t):lib.nrf_keys_tick(keys,C.c_uint32(t))
    edge(1,True,100);edge(1,False,150);tick(399);assert events==[];tick(400);assert events==[(1,0)]
    events.clear();edge(1,True,500);edge(1,False,540);edge(1,True,600);edge(1,False,640);tick(1000);assert events==[(1,1)]
    events.clear();edge(1,True,1200);tick(1850);edge(1,False,1900);tick(2200);assert events==[(1,2)]
    events.clear();edge(0,True,2300);edge(0,False,2340);edge(0,True,2400);edge(0,False,2440);tick(2800);assert events==[(0,0),(0,1)]
    events.clear();edge(2,True,3000);tick(3650);tick(3870);tick(4100);edge(2,False,4200);tick(4500);assert events==[(2,2),(2,3),(2,3)]
    # Debounce duplicates and uint32 millisecond wrap preserve the same gestures.
    events.clear();edge(1,True,0xfffffff0);edge(1,True,0xfffffff1);edge(1,False,20);tick(271);assert events==[(1,0)]
    unload_library(lib._handle)
    print(f'PASS: {checks} production sprite actions, {frames} frames, delta seeks/loops, corrupt-input checks; {cat.count} catalog records, stable IDs and GZJT search; 6 key-timing scenarios')
