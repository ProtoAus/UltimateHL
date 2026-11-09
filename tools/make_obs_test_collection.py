"""Create a NEW display-only WGC scene collection; never edit OBS's saved files."""
import argparse
import copy
import json
from pathlib import Path
import uuid


def make(source: Path, destination: Path, kind="wgc"):
    if kind not in ("wgc","game"):raise ValueError("Unknown capture method")
    if destination.exists(): raise ValueError("Choose a NEW output file")
    old = json.loads(source.read_text(encoding="utf-8-sig"))
    display = next(s for s in old["sources"] if s.get("id") == "monitor_capture")
    scene = next(s for s in old["sources"] if s.get("id") == "scene")
    capture = copy.deepcopy(display)
    capture.update(name="UltimateHL - Display WGC", uuid=str(uuid.uuid4()), filters=[], hotkeys={}, private_settings={})
    # OBS 32 duplicator-monitor-capture.c enum: AUTO=0, DXGI=1, WGC=2.
    capture.setdefault("settings", {}).update(method=2, cursor=True)
    title="UltimateHL Display WGC Test"
    if kind=="game":
        title="UltimateHL Full Resolution Game Capture"
        capture.update(name="UltimateHL - Game Capture",id="game_capture",versioned_id="game_capture",
            settings={"capture_mode":"window","window":"Half-Life:Valve001:hl.exe","priority":2,
                "capture_cursor":True,"capture_overlays":False,"anti_cheat_hook":False,"hook_rate":0})
    capture["enabled"] = True
    capture["mixers"] = 0
    new_scene = copy.deepcopy(scene)
    new_scene.update(name=title, uuid=str(uuid.uuid4()), hotkeys={}, private_settings={})
    resolution = old.get("resolution", {"x":1920,"y":1200})
    width, height = resolution["x"], resolution["y"]
    item = {"name":capture["name"],"source_uuid":capture["uuid"],"visible":True,"locked":False,
            "rot":0.0,"scale_ref":{"x":float(width),"y":float(height)},"align":5,
            "bounds_type":2,"bounds_align":0,"bounds_crop":False,
            "crop_left":0,"crop_top":0,"crop_right":0,"crop_bottom":0,"id":1,"group_item_backup":False,
            "pos":{"x":0.0,"y":0.0},"pos_rel":{"x":-width/height,"y":-1.0},
            "scale":{"x":1.0,"y":1.0},"scale_rel":{"x":1.0,"y":1.0},
            "bounds":{"x":float(width),"y":float(height)},"bounds_rel":{"x":2*width/height,"y":2.0},
            "scale_filter":"disable","blend_method":"default","blend_type":"normal",
            "show_transition":{"duration":0},"hide_transition":{"duration":0},"private_settings":{}}
    new_scene["settings"] = {"id_counter":1,"custom_size":False,"items":[item]}
    result = {"name":title,"sources":[capture,new_scene],"groups":[],"canvases":[],
              "scene_order":[{"name":new_scene["name"]}],"current_scene":new_scene["name"],
              "current_program_scene":new_scene["name"],"resolution":resolution,"version":old.get("version",2),
              "DesktopAudioDevice1":None,"AuxAudioDevice1":None,"transitions":[],"quick_transitions":[],
              "saved_projectors":[],"preview_locked":False,"scaling_enabled":False}
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(f"Created {destination}; saved OBS collections/profiles unchanged. Import manually into OBS.")


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source",type=Path)
    parser.add_argument("destination",type=Path)
    parser.add_argument("--kind",choices=("wgc","game"),default="wgc")
    args=parser.parse_args()
    make(args.source,args.destination,args.kind)
