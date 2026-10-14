# SPDX-License-Identifier: GPL-3.0-or-later
"""Pure SysEx packets + bounded read transport. No write executor yet.
Payloads exclude F0/F7 (mido conventions). Write packets are offline only.
"""
import time
from .codec import SIZE, SECTOR

PREFIX=bytes((0,1,0x72,0x49))

def nibbles(value,count):
    if type(value) is not int or not 0<=value<1<<(count*4):
        raise ValueError('Nibble integer out of range')
    return bytes((value>>shift)&15 for shift in range((count-1)*4,-1,-4))

def unnibbles(data):
    if any(v>15 for v in data): raise ValueError('Noncanonical nibble')
    value=0
    for v in data: value=(value<<4)|v
    return value

def read_packet(address,length):
    if not 1<=length<=64 or not 0<=address<=0x200000-length:
        raise ValueError('Read range out of bounds')
    return PREFIX+b'\x70'+nibbles(address,6)+bytes((length,))

def decode_read(payload,address,length):
    data=bytes(payload)
    if data[:4]!=PREFIX: return None
    if len(data)>=5 and data[4]==1:
        if len(data)!=7: raise ValueError('Malformed error reply')
        if data[5] in (0x70,0x71):
            raise ValueError(f'Device read error{data[6]:02x}')
        return None
    if len(data)<5 or data[4]!=0x71: return None
    if len(data)!=12+2*length:
        raise ValueError('Read response length mismatch')
    if unnibbles(data[5:11])!=address or data[11]!=length:
        raise ValueError('Read response address/count mismatch')
    return bytes(unnibbles(data[i:i+2]) for i in range(12,len(data),2))

def stage_packet(offset,data):
    data=bytes(data)
    if not 1<=len(data)<=64 or not 0<=offset<=SECTOR-len(data):
        raise ValueError('Stage range out of bounds')
    return PREFIX+b'\x72'+nibbles(offset,6)+bytes((len(data),))+b''.join(nibbles(v,2) for v in data)

def commit_packet(address):
    if type(address) is not int or address%SECTOR or not 0<=address<=64*SIZE-SECTOR:
        raise ValueError('Commit must address complete aligned song sector')
    return PREFIX+b'\x73'+nibbles(address,6)+nibbles(SECTOR,4)

def sector_packets(address,data):
    """Offline sequence: stage offset0 FIRST, wait for each ACK before next.
    Commit OK arrives only at completion. No automatic retries on timeout.
    """
    data=bytes(data)
    if len(data)!=SECTOR: raise ValueError('Complete sector required')
    commit=commit_packet(address)
    return [stage_packet(o,data[o:o+64]) for o in range(0,SECTOR,64)]+[commit]

def read_song(send,receive,*,slot,timeout=3.0,pacing=0.005):
    """Callbacks exchange SysEx payloads; receive(deadline) must be bounded.
    Caller opens correct USB port1, stops transport, avoids panel I/O.
    No pipelining or retry; exceptions abort capture.
    """
    if type(slot) is not int or not 1<=slot<=64: raise ValueError('Choose slot1..64')
    if timeout<=0 or pacing<0: raise ValueError('Invalid timing settings')
    result=bytearray()
    for address in range((slot-1)*SIZE,slot*SIZE,64):
        send(read_packet(address,64))
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            payload=receive(deadline)
            if payload is None: raise TimeoutError('Read timeout; no retry')
            decoded=decode_read(payload,address,64)
            if decoded is not None:
                result.extend(decoded); break
        else: raise TimeoutError('Read timeout; no retry')
        time.sleep(pacing)
    return bytes(result)
