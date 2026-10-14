# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared events; scene parameters are NOT per-scene event banks."""
from dataclasses import dataclass

TYPES = {0: 'Empty', 1: 'CCi', 2: 'Prb', 3: 'Euc', 4: 'Pre',
         5: '!Pr', 6: 'Hrm', 7: 'Inv', 0x90: 'Note', 0xb0: 'CC', 0xc0: 'PC'}

@dataclass(frozen=True)
class Event:
    type: int
    data0: int
    data1: int
    padding: int
    length: int

    def validate(self):
        if self.type not in TYPES:
            raise ValueError('Unknown event type; preserve raw, do not author it')
        if not all(type(v) is int and 0 <= v <= 255 for v in
                   (self.type, self.data0, self.data1, self.padding)):
            raise ValueError('Event bytes out of range')
        if type(self.length) is not int or not 0 <= self.length <= 65535:
            raise ValueError('Length must be uint16 ticks')
        if self.type == 0:  # inactive payload is deliberately opaque
            return
        limits = {1: (29,127), 2:(100,None), 3:(27,7),
                  6:(16,13), 7:(5,1), 0x90:(127,127),
                  0xb0:(127,127), 0xc0:(127,None)}
        if self.type == 1 and self.data0 < 16:
            raise ValueError('CCi code must be16..29')
        a,b = limits.get(self.type,(None,None))
        if (a is not None and self.data0 > a) or (b is not None and self.data1 > b):
            raise ValueError('Event parameter out of range')

def event_key(track, step, slot):
    if any(type(v) is not int or not 1 <= v <= n
           for v,n in zip((track,step,slot),(6,64,6))):
        raise ValueError('Use track1..6, step1..64, slot1..6')
    return track,step,slot
