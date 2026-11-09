"""Bounded staged v3.2 save/load/renderer QA. Refuses any existing hl.exe; no input injection."""
from pathlib import Path
import argparse
import csv
import ctypes as c
from ctypes import wintypes as w
import json
import os
import subprocess
import uuid

from test_held_input import video_snapshot,restore_video


def running_games():
    result=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-Command',
        "Get-CimInstance Win32_Process -Filter \"name = 'hl.exe'\" | Select-Object ProcessId,ParentProcessId,ExecutablePath | ConvertTo-Json -Compress"],
        check=True,capture_output=True,text=True,timeout=20)
    text=result.stdout.strip()
    if not text:return []
    value=json.loads(text)
    return [value] if isinstance(value,dict) else value


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('installation',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--allow-launch',action='store_true')
    parser.add_argument('--settings-test',action='store_true',help='Open the owned settings panel before quitting')
    parser.add_argument('--capture-safe',action='store_true')
    parser.add_argument('--trigger-test',action='store_true')
    parser.add_argument('--cheats-test',action='store_true')
    parser.add_argument('--bxt-features-test',action='store_true')
    parser.add_argument('--cheats-stock-control',action='store_true')
    parser.add_argument('--no-load-floor',action='store_true')
    parser.add_argument('--hud-scale',type=float,choices=(1,1.5,2),default=1)
    parser.add_argument('--frame-dump',action='store_true')
    parser.add_argument('--legacy-present',action='store_true')
    parser.add_argument('--warmup-frames',type=int,default=100)
    parser.add_argument('--default-render',action='store_true',help='Use saved/default scene scale, without a launch override')
    parser.add_argument('--native-control',action='store_true',help='Consume launch intent, then select native 1x before rendering')
    args=parser.parse_args();root=args.installation.resolve();game=root/'Half-Life';mod=game/'valve_WON'
    if not 1<=args.warmup_frames<=1000:raise ValueError('Warmup must be 1..1000 frames')
    if not args.allow_launch:raise ValueError('Explicit --allow-launch required')
    if args.output.exists()or not args.output.parent.is_dir():raise ValueError('Choose a NEW report path in an existing directory')
    manifest=json.loads((root/'stage-manifest.json').read_text())
    if manifest.get('build_version')not in ('3.2','3.3','3.4','3.5','3.6','3.7','3.7.1'):raise ValueError('Only a separately staged supported v3.x installation is allowed')
    result={'input_injected':False,'foreground_forced':False,'mode':('OBS-safe'if args.capture_safe else 'normal')+' borderless, BXT, scene scale 2'}
    if running_games():
        result.update(status='skipped',reason='An hl.exe is running; it was left alone and no diagnostic game launched.')
        args.output.write_text(json.dumps(result,indent=2)+'\n');print(result);return
    token=uuid.uuid4().hex[:6];script=mod/f'uhlqa32_{token}.cfg';save=f'uhlq{token}'
    config_paths=(mod/'config.cfg',mod/'userconfig.cfg',mod/'autoexec.cfg',mod/'uhl_preferences.cfg')
    if args.settings_test:config_paths+=tuple((mod/'sprites').glob('*.txt'))
    configs={p:(p.read_bytes(),p.stat())for p in config_paths if p.exists()}
    savedir=mod/'SAVE';saves={p.name:(p.read_bytes(),p.stat())for p in savedir.glob('*')if p.is_file()}
    before_traces=set(game.glob('uhl-input-*.tsv'));before_diags=set(game.glob('uhl-diagnostics-*.txt'))
    log=game/'qconsole.log';old_log_size=log.stat().st_size if log.exists()else 0
    settings_commands='uhl_settings\n__q10\n'if args.settings_test else ''
    scale_commands='uhl_apply_launch\nuhl_render_scale 1'if args.native_control else 'uhl_render_scale 1'
    if args.default_render:scale_commands='';result['mode']='normal default-render launch, BXT'
    if args.native_control:result['mode']+='; manual native-1 control after startup override'
    warmup_commands='\n'.join(['__q100']*(args.warmup_frames//100)+['wait']*(args.warmup_frames%100))
    script.write_text(f'''exec config.cfg
exec uhl_defaults.cfg
uhl_render_debug 1
// Explicit launch override, or manual native-1 comparison after consuming it.
{scale_commands}
{'sv_cheats 1;deathmatch 1;map crossfire' if args.frame_dump else 'deathmatch 0;map crossfire'}
alias __q10 "wait;wait;wait;wait;wait;wait;wait;wait;wait;wait"
alias __q100 "__q10;__q10;__q10;__q10;__q10;__q10;__q10;__q10;__q10;__q10"
{'sv_cheats 1;impulse 101;__q10;weapon_9mmhandgun;__q10' if args.frame_dump else ''}
{warmup_commands}
{'uhl_frame_dump' if args.frame_dump else ''}
uhl_trace_start
uhl_status
+forward 119
__q10
save {save}
__q10
uhl_status
-forward 119
uhl_fps 4
load {save}
uhl_fps 4
__q10
uhl_status
uhl_render_test
uhl_diagnostics
{settings_commands}uhl_trace_stop
quit
''')
    if args.cheats_test:
        script.write_text('''exec config.cfg
exec uhl_defaults.cfg
uhl_fps 100
sv_cheats 0
map crossfire
alias __q10 "wait;wait;wait;wait;wait;wait;wait;wait;wait;wait"
alias __q100 "__q10;__q10;__q10;__q10;__q10;__q10;__q10;__q10;__q10;__q10"
__q100
echo UHL_CHEATS_BEFORE
sv_cheats
god
noclip
notarget
__q10
uhl_status
sv_cheats 1
echo UHL_CHEATS_AFTER
sv_cheats
god
noclip
notarget
impulse 101
__q100
uhl_status
uhl_frame_dump
god
noclip
__q10
sv_cheats 0
__q10
god
noclip
__q100
uhl_status
quit
''')
    if args.trigger_test:
        from bsp_trigger import normal_changelevel
        trigger=normal_changelevel(game/'valve/maps/c1a0.bsp');result['trigger']=trigger
        xyz=' '.join(str(value)for value in trigger['center'])
        script.write_text(f'''exec config.cfg
exec uhl_defaults.cfg
uhl_render_debug 1
map c1a0
alias __q10 "wait;wait;wait;wait;wait;wait;wait;wait;wait;wait"
alias __q100 "__q10;__q10;__q10;__q10;__q10;__q10;__q10;__q10;__q10;__q10"
__q100
sv_cheats 1
uhl_cursor_lock 1
uhl_load_mouse_freeze 1
uhl_load_fps_min 100
uhl_fps 4
uhl_trace_start
uhl_status
// Set only the server player position in this owned offline test. The original
// Original trigger/physics performs the transition. NO load/changelevel command.
// Leave Cbuf empty after teleport: the original trigger appends changelevel
// to its tail. A queued quit would otherwise execute BEFORE that transition.
bxt_ch_set_pos {xyz}
''')
    script.write_text(script.read_text().replace('exec uhl_defaults.cfg\n',f'exec uhl_defaults.cfg\nuhl_hud_scale {args.hud_scale}\n'))
    if args.bxt_features_test:
        queries='\n'.join(('echo UHL_TANKFIRD_FEATURES','bxt_hud_entities_min','bxt_hud_entities_max','bxt_hud_entities_avel','bxt_ch_set_groundentity',
            'bxt_ch_rot_pos','bxt_ch_rot_ang','bxt_ch_rot_avel','bxt_ch_rot_ft','bxt_show_rotate_trace','bxt_hud_rotate_trace',
            'bxt_hud_physents','cmdlist bxt_ch_','bxt_hud_speedometer 1'))+'\n'
        # Server-side BXT cvars register after the first active server frame.
        # Query after warmup, not immediately after the map command.
        anchor='uhl_frame_dump\n'if 'uhl_frame_dump\n'in script.read_text()else 'uhl_trace_start\n'
        script.write_text(script.read_text().replace(anchor,queries+'__q10\n'+anchor,1))
    if args.no_load_floor:
        script.write_text(script.read_text().replace('uhl_load_fps_min 100','uhl_load_fps_min 0'))
    if args.cheats_stock_control:
        if not args.cheats_test:raise ValueError('Stock cheat control requires --cheats-test')
        script.write_text(script.read_text().replace('sv_cheats 0\nmap crossfire','uhl_cheats_live 0\nsv_cheats 0\nmap crossfire'))
    video=video_snapshot();launcher=None;pid=None;handle=None
    kernel=c.WinDLL('kernel32');kernel.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];kernel.OpenProcess.restype=w.HANDLE
    kernel.WaitForSingleObject.argtypes=[w.HANDLE,w.DWORD];kernel.CloseHandle.argtypes=[w.HANDLE]
    try:
        options=['--capture-safe']if args.capture_safe else []
        if args.legacy_present:options.append('--legacy-present')
        if not args.default_render:options+=['--render-scale','2']
        launcher=subprocess.Popen([str(root/'UltimateHL.exe'),*options,'--','-nosound','+exec',script.name],cwd=root,
            stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        if launcher.wait(timeout=45)!=0:raise RuntimeError('Launcher validation failed')
        receipt=next(game.glob(f'uhl-startup-{launcher.pid}-*.txt'))
        fields=dict(line.split('=',1)for line in receipt.read_text().splitlines()if'='in line);pid=int(fields['pid'])
        # Verify the selected PID path before acquiring a termination right.
        matches=[p for p in running_games()if p['ProcessId']==pid]
        if matches and Path(matches[0]['ExecutablePath']).resolve()!=game/'hl.exe':raise RuntimeError('Unexpected diagnostic process path')
        handle=kernel.OpenProcess(0x100001,False,pid)
        if args.trigger_test and handle and kernel.WaitForSingleObject(handle,6500)!=0:
            # The transition itself owns Cbuf now. Close only this verified PID's
            # game window rather than queuing a console quit ahead of changelevel.
            user=c.WinDLL('user32');callback=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
            user.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
            user.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,c.c_int]
            user.PostThreadMessageW.argtypes=[w.DWORD,w.UINT,w.WPARAM,w.LPARAM]
            user.EnumWindows.argtypes=[callback,w.LPARAM]
            @callback
            def close_owned(hwnd,_):
                owner=w.DWORD();name=c.create_unicode_buffer(64)
                thread=user.GetWindowThreadProcessId(hwnd,c.byref(owner));user.GetClassNameW(hwnd,name,64)
                if owner.value==pid and name.value=='Valve001':user.PostThreadMessageW(thread,0x12,0,0)
                return True
            user.EnumWindows(close_owned,0)
        if handle and kernel.WaitForSingleObject(handle,20000)!=0:raise TimeoutError('Diagnostic child exceeded bound')
        text=log.read_bytes()[old_log_size:].decode(errors='replace')
        trace=next(iter(set(game.glob('uhl-input-*.tsv'))-before_traces),None)
        rows=[]
        if trace:
            with trace.open()as file:rows=list(csv.DictReader(file,delimiter='\t'))
        frames=[r for r in rows if r['event']=='load_fps_frame']
        result.update(status='completed',receipt=receipt.read_text(),log=text,
            load_frame_caps=[int(r['a'])for r in frames],
            load_frame_intervals=[round(float(b['seconds'])-float(a['seconds']),6)for a,b in zip(frames,frames[1:])],
            menu_activations_blocked=sum(r['event']=='load_menu_blocked'for r in rows),
            gameplay_restores=[r for r in rows if r['event']in ('load_fps_clear_resume','load_fps_gameplay_fallback')],
            direct_bound_fps='not tested: no desktop key messages injected',
            ui_loads=[r for r in rows if r['event']=='load_ui_begin'],
            ui_freeze=[r for r in rows if r['event']=='load_ui_mouse'],
            ui_clip=[r for r in rows if r['event']=='load_ui_clip'],
            trace=str(trace)if trace else None,
            diagnostics=[str(p)for p in set(game.glob('uhl-diagnostics-*.txt'))-before_diags])
        if args.cheats_test:
            import re
            states=[dict(requested=int(a),engine=int(b),gamedll=int(d),weapons=int(e,16),suit=int(f))for a,b,d,e,f in
                re.findall(r'requested=(\d+), server latch=(\d+), GameDLL impulse latch=(\d+), weapons=([0-9a-f]+) suit=(\d+)',text)]
            result['cheats_states']=states
            result['same_map_impulse_101_granted']=any(s['requested']==1 and s['suit']==1 and s['weapons']&0x7ffe==0x7fde for s in states)
        print('\n'.join(line for line in text.splitlines()if any(word in line for word in
            ['Scene SSAA:','Look modes:','FPS:','GPU self-test','GameUI007','load-menu','atlas inset','Level:','Presentation:','Renderer:','Practice cheats:','HUD canvas:'])))
    finally:
        if not handle and launcher:
            children=[p for p in running_games()if p['ParentProcessId']==launcher.pid and
                Path(p['ExecutablePath']).resolve()==game/'hl.exe']
            if len(children)==1:handle=kernel.OpenProcess(0x100001,False,children[0]['ProcessId'])
        if handle:
            if kernel.WaitForSingleObject(handle,0)!=0:
                kernel.TerminateProcess.argtypes=[w.HANDLE,w.UINT];kernel.TerminateProcess(handle,1)
                kernel.WaitForSingleObject(handle,5000)
            kernel.CloseHandle(handle)
        if launcher and launcher.poll()is None:launcher.terminate();launcher.wait()
        script.unlink(missing_ok=True)
        for p in config_paths:
            if p not in configs:p.unlink(missing_ok=True)
        for p,(data,stat)in configs.items():
            if not p.exists()or p.read_bytes()!=data:p.write_bytes(data)
            os.utime(p,ns=(stat.st_atime_ns,stat.st_mtime_ns))
        for p in savedir.glob('*'):
            if p.is_file()and p.name not in saves:p.unlink()
        for name,(data,stat)in saves.items():
            p=savedir/name
            if not p.exists()or p.read_bytes()!=data:p.write_bytes(data)
            os.utime(p,ns=(stat.st_atime_ns,stat.st_mtime_ns))
        restore_video(video)
    args.output.write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':main()
