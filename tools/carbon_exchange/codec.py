# SPDX-License-Identifier: GPL-3.0-or-later
"""Lossless1.34 raw image codec; unmodelled bytes remain opaque."""
import hashlib
import struct
from dataclasses import dataclass, replace
from .model import Event, TYPES, event_key

SIZE = 0x5000
SECTOR = 0x1000
EVENT_BASE = 1824
STEP_BASE = 1056
VERSION = 0x10022

@dataclass(frozen=True)
class SongImage:
    raw: bytes

    def __post_init__(self):
        if not isinstance(self.raw, bytes) or len(self.raw) != SIZE:
            raise ValueError('Song image must be exactly0x5000 bytes')
        if struct.unpack_from('<I', self.raw, SIZE-4)[0] != 0x534f4e47:
            raise ValueError('Invalid SONG magic')

    @property
    def sha256(self):
        return hashlib.sha256(self.raw).hexdigest()

    @property
    def version(self):
        return struct.unpack_from('<I', self.raw)[0]

    @staticmethod
    def offset(track, step, slot):
        event_key(track,step,slot)
        return EVENT_BASE + ((track-1)*64*6 + (step-1)*6 + slot-1)*6

    def event(self,track,step,slot):
        return Event(*struct.unpack_from('<BBBBH',self.raw,self.offset(track,step,slot)))

    def patch_event(self,track,step,slot,*,base_sha256,**fields):
        if self.sha256 != base_sha256:
            raise ValueError('Stale base: SHA256 mismatch')
        if self.version != VERSION:
            raise ValueError('Authoring restricted to verified1.34 layout/version')
        if not fields or set(fields)-{'type','data0','data1','length'}:
            raise ValueError('Patch requires explicit supported fields')
        old=self.event(track,step,slot)
        new=replace(old,**fields)
        new.validate()
        data=bytearray(self.raw)
        struct.pack_into('<BBBBH',data,self.offset(track,step,slot),
                         new.type,new.data0,new.data1,new.padding,new.length)
        return SongImage(bytes(data))

    def encode(self):
        return self.raw


def semantic_diff(before,after):
    changes=[]
    covered=set()
    for track in range(1,7):
        for step in range(1,65):
            for slot in range(1,7):
                old,new=before.event(track,step,slot),after.event(track,step,slot)
                if old != new:
                    changes.append({'track':track,'step':step,'slot':slot,
                                    'before':old.__dict__,'after':new.__dict__})
                    o=SongImage.offset(track,step,slot)
                    covered.update(range(o,o+6))
    offsets=[i for i,(a,b) in enumerate(zip(before.raw,after.raw)) if a!=b]
    return {'event_changes':changes,'changed_bytes':offsets,
            'opaque_changes':[i for i in offsets if i not in covered],
            'affected_sectors':sorted({i//SECTOR for i in offsets})}

def sector_plan(before,after,*,slot,mode):
    """Offline only: mode=replace or patch; panel slots are one-based."""
    if type(slot) is not int or not 1<=slot<=64:
        raise ValueError('Explicit panel slot1..64 required')
    if mode not in ('replace','patch'):
        raise ValueError('Explicit replace/patch mode required')
    if after.version!=VERSION:
        raise ValueError('Target must be verified1.34 image')
    # Validate active events; NULL stale bytes remain faithfully preserved.
    for t in range(1,7):
        for s in range(1,65):
            for p in range(1,7): after.event(t,s,p).validate()
    diff=semantic_diff(before,after)
    indices=range(5) if mode=='replace' else diff['affected_sectors']
    return {'mode':mode,'slot':slot,'base_sha256':before.sha256,
            'result_sha256':after.sha256,'diff':diff,
            'sectors':[{'address':(slot-1)*SIZE+i*SECTOR,
                        'data':after.raw[i*SECTOR:(i+1)*SECTOR]} for i in indices]}
