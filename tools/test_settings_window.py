"""Exercise ONLY an owned v3.3 native settings panel. No desktop input/focus injection."""
from pathlib import Path
import ctypes as c
from ctypes import wintypes as w
import json
import subprocess
import sys
import time
from PIL import Image


def text(path):
    try:return path.read_text()
    except OSError:return ''


def main():
    if len(sys.argv)!=4:raise ValueError('installation output-prefix --allow-launch required')
    root=Path(sys.argv[1]).resolve();prefix=Path(sys.argv[2]);flag=sys.argv[3]
    if flag!='--allow-launch':raise ValueError('Explicit launch flag required')
    if json.loads((root/'stage-manifest.json').read_text()).get('build_version')not in ('3.3','3.4','3.5','3.6'):raise ValueError('Only a separately staged v3.3/v3.4/v3.5/v3.6 copy is allowed')
    runtime=prefix.with_suffix('.runtime.json');result_file=prefix.with_suffix('.json');image=prefix.with_suffix('.png')
    if any(p.exists()for p in (runtime,result_file,image)):raise ValueError('Choose a NEW output prefix')
    old=set((root/'Half-Life').glob('uhl-startup-*.txt'))
    command=[sys.executable,str(Path(__file__).with_name('test_v32_offline.py')),str(root),str(runtime),flag,'--settings-test']
    user=c.WinDLL('user32');gdi=c.WinDLL('gdi32')
    user.SetProcessDPIAware() # Observer-only: do not crop a physical PrintWindow with virtualized dimensions.
    process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    user.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
    user.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,c.c_int]
    user.GetDlgItem.argtypes=[w.HWND,c.c_int];user.GetDlgItem.restype=w.HWND
    user.SendMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];user.SendMessageW.restype=c.c_ssize_t
    user.GetWindowRect.argtypes=[w.HWND,c.POINTER(w.RECT)]
    user.GetWindowDC.argtypes=[w.HWND];user.GetWindowDC.restype=w.HDC
    user.PrintWindow.argtypes=[w.HWND,w.HDC,w.UINT];user.ReleaseDC.argtypes=[w.HWND,w.HDC]
    gdi.CreateCompatibleDC.argtypes=[w.HDC];gdi.CreateCompatibleDC.restype=w.HDC
    gdi.CreateCompatibleBitmap.argtypes=[w.HDC,c.c_int,c.c_int];gdi.CreateCompatibleBitmap.restype=w.HBITMAP
    gdi.SelectObject.argtypes=[w.HDC,w.HGDIOBJ];gdi.SelectObject.restype=w.HGDIOBJ
    gdi.DeleteObject.argtypes=[w.HGDIOBJ];gdi.DeleteDC.argtypes=[w.HDC]
    class Header(c.Structure):
        _fields_=[('size',w.DWORD),('width',w.LONG),('height',w.LONG),('planes',w.WORD),('bits',w.WORD),
            ('compression',w.DWORD),('imageSize',w.DWORD),('xppm',w.LONG),('yppm',w.LONG),('used',w.DWORD),('important',w.DWORD)]
    gdi.GetDIBits.argtypes=[w.HDC,w.HBITMAP,w.UINT,w.UINT,c.c_void_p,c.c_void_p,w.UINT]
    callback=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM);user.EnumWindows.argtypes=[callback,w.LPARAM]
    result={};end=time.monotonic()+40
    try:
        while process.poll()is None and time.monotonic()<end:
            new=set((root/'Half-Life').glob('uhl-startup-*.txt'))-old
            ready=[f for f in new if 'hud_ready=1'in text(f)]
            if not ready:time.sleep(.04);continue
            fields=dict(line.split('=',1)for line in text(ready[0]).splitlines()if'='in line)
            if 'pid'not in fields:continue
            pid=int(fields['pid']);found=[]
            @callback
            def enum(hwnd,_):
                owner=w.DWORD();user.GetWindowThreadProcessId(hwnd,c.byref(owner))
                if owner.value==pid:
                    title=c.create_unicode_buffer(256);user.GetWindowTextW(hwnd,title,256)
                    if title.value=='UltimateHL settings':found.append(hwnd)
                return True
            user.EnumWindows(enum,0)
            if not found:time.sleep(.04);continue
            hwnd=found[0];checkbox=user.GetDlgItem(hwnd,108);apply=user.GetDlgItem(hwnd,110)
            if not checkbox or not apply:time.sleep(.04);continue
            fps_absent=not user.GetDlgItem(hwnd,113) and not user.GetDlgItem(hwnd,114)
            # Commands go only to controls of the explicitly created child PID.
            user.SendMessageW(checkbox,0xf1,1,0);user.SendMessageW(hwnd,0x111,110,0)
            prefs=root/'Half-Life/valve_WON/uhl_preferences.cfg';on='uhl_msaa 1'in text(prefs)
            user.SendMessageW(checkbox,0xf1,0,0);user.SendMessageW(hwnd,0x111,110,0)
            off='uhl_msaa 0'in text(prefs)
            hud=user.GetDlgItem(hwnd,115);art=user.GetDlgItem(hwnd,109)
            hud_scales=[]
            for index,value in ((1,'1.5'),(2,'2'),(0,'1')):
                user.SendMessageW(hud,0x14e,index,0);user.SendMessageW(hwnd,0x111,110,0)
                hud_scales.append(f'uhl_hud_scale {value}\n'in text(prefs))
            user.SendMessageW(art,0x14e,2,0);user.SendMessageW(hwnd,0x111,110,0)
            ag_selected=(root/'Half-Life/valve_WON/sprites/hud.txt').read_bytes()==(root/'HUD-presets/ag/hud.txt').read_bytes()
            user.SendMessageW(art,0x14e,0,0);user.SendMessageW(hwnd,0x111,110,0)
            stock_selected=(root/'Half-Life/valve_WON/sprites/hud.txt').read_bytes()==(root/'HUD-presets/stock/hud.txt').read_bytes()
            rect=w.RECT();user.GetWindowRect(hwnd,c.byref(rect));width=rect.right-rect.left;height=rect.bottom-rect.top
            dc=user.GetWindowDC(hwnd);mem=gdi.CreateCompatibleDC(dc);bitmap=gdi.CreateCompatibleBitmap(dc,width,height)
            previous=gdi.SelectObject(mem,bitmap)
            try:
                printed=bool(user.PrintWindow(hwnd,mem,2));head=Header(c.sizeof(Header),width,-height,1,32,0,width*height*4,0,0,0,0)
                pixels=c.create_string_buffer(width*height*4);lines=gdi.GetDIBits(mem,bitmap,0,height,pixels,c.byref(head),0)
                if lines:Image.frombytes('RGB',(width,height),pixels.raw,'raw','BGRX').save(image)
            finally:
                gdi.SelectObject(mem,previous);gdi.DeleteObject(bitmap);gdi.DeleteDC(mem);user.ReleaseDC(hwnd,dc)
            clip=w.RECT();user.GetClipCursor(c.byref(clip))
            result=dict(owned_pid=pid,panel_size=[width,height],apply_msaa_on_saved=on,apply_msaa_off_saved=off,
                printed=printed,screenshot_rows=lines,clip_while_settings=[clip.left,clip.top,clip.right,clip.bottom],
                fps_buttons_absent=fps_absent,hud_scales_saved=hud_scales,ag_preset_selected=ag_selected,stock_preset_restored=stock_selected,
                global_input_injected=False,foreground_forced=False)
            break
    finally:
        output,_=process.communicate(timeout=65);print(output)
    result_file.write_text(json.dumps(result,indent=2)+'\n');print(result)
    if not(result.get('apply_msaa_on_saved')and result.get('apply_msaa_off_saved')and result.get('fps_buttons_absent')and all(result.get('hud_scales_saved',[]))and result.get('ag_preset_selected')and result.get('stock_preset_restored')):
        raise RuntimeError('Owned settings window/apply/persistence not observed; see runtime report')


if __name__=='__main__':main()
