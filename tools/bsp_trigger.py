"""Read an original GoldSrc BSP; never edit map entities or game DLLs."""
from pathlib import Path
import re
import struct


def normal_changelevel(path: Path) -> dict:
    data=path.read_bytes()
    if struct.unpack_from('<i',data)[0]!=30:raise ValueError('Expected GoldSrc BSP version 30')
    offset,length=struct.unpack_from('<ii',data,4)
    models,_=struct.unpack_from('<ii',data,4+14*8)
    for block in re.findall(r'\{([^{}]*)\}',data[offset:offset+length].decode('latin1')):
        entity=dict(re.findall(r'"([^"]+)"\s*"([^"]*)"',block))
        if entity.get('classname')!='trigger_changelevel' or entity.get('targetname') or int(entity.get('spawnflags','0'))&2:continue
        model=int(entity['model'][1:]);bounds=struct.unpack_from('<6f',data,models+model*64)
        return dict(entity=entity,center=[(bounds[i]+bounds[i+3])/2 for i in range(3)],bounds=bounds)
    raise ValueError('No ordinary touch-activated changelevel found')
