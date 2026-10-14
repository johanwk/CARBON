# SPDX-License-Identifier: GPL-3.0-or-later
import struct
import unittest
from tools.carbon_exchange.codec import SongImage, SIZE, VERSION, semantic_diff, sector_plan
from tools.carbon_exchange.model import Event
from tools.carbon_exchange.transport import (PREFIX,nibbles,read_packet,decode_read,
    stage_packet,commit_packet,sector_packets,read_song)

def image():
    data=bytearray((i*17)&255 for i in range(SIZE))
    struct.pack_into('<I',data,0,VERSION); struct.pack_into('<I',data,SIZE-4,0x534f4e47)
    for t in range(1,7):
        for s in range(1,65):
            for p in range(1,7): data[SongImage.offset(t,s,p)]=0
    struct.pack_into('<BBBBH',data,SongImage.offset(1,1,2),0x90,48,96,255,106)
    return SongImage(bytes(data))

class ExchangeTests(unittest.TestCase):
    def test_lossless(self):
        song=image(); self.assertEqual(song.encode(),song.raw)
        self.assertEqual(song.event(1,1,2),Event(0x90,48,96,255,106))
        self.assertEqual(song.offset(6,64,6),15642)
        with self.assertRaises(ValueError): SongImage(b'')
        with self.assertRaises(ValueError): SongImage(bytes(SIZE))

    def test_patch_diff(self):
        before=image(); after=before.patch_event(1,1,2,base_sha256=before.sha256,data1=97)
        d=semantic_diff(before,after)
        self.assertEqual(d['changed_bytes'],[1832]); self.assertEqual(d['opaque_changes'],[])
        self.assertEqual(d['affected_sectors'],[0]); self.assertEqual(len(d['event_changes']),1)
        self.assertEqual(before.event(1,1,1),after.event(1,1,1))
        self.assertEqual(after.event(1,1,2).padding,255)
        self.assertEqual(len(sector_plan(before,after,slot=64,mode='patch')['sectors']),1)
        self.assertEqual(len(sector_plan(before,after,slot=64,mode='replace')['sectors']),5)
        with self.assertRaises(ValueError): before.patch_event(1,1,2,base_sha256='stale',data1=97)
        with self.assertRaises(ValueError): before.patch_event(1,1,2,base_sha256=before.sha256,data1=128)
        with self.assertRaises(ValueError): before.event(0,1,1)

    def test_event_types(self):
        for e in (Event(1,29,127,0,0),Event(2,100,0,0,0),Event(3,27,7,0,0),
                  Event(4,255,255,0,65535),Event(5,255,255,0,0),
                  Event(6,16,13,0,0),Event(7,5,1,0,0),
                  Event(0xb0,74,127,0,0),Event(0xc0,127,255,0,0)):
            e.validate()
        with self.assertRaises(ValueError): Event(7,6,1,0,0).validate()

    def test_packets(self):
        self.assertEqual(len(read_packet(0x5a000,64)),12)
        for n in range(1,65): self.assertEqual(len(stage_packet(4096-n,bytes(range(n)))),12+2*n)
        packets=sector_packets(0x5a000,bytes(range(256))*16)
        self.assertEqual(len(packets),65); self.assertEqual(packets[-1],commit_packet(0x5a000))
        self.assertEqual(packets[0][5:11],bytes(6))
        for addr in (1,64*SIZE,-4096):
            with self.assertRaises(ValueError): commit_packet(addr)
        with self.assertRaises(ValueError): stage_packet(4095,b'xx')
        response=PREFIX+b'\x71'+nibbles(0x5a000,6)+b'\x01\x0a\x0b'
        self.assertEqual(decode_read(response,0x5a000,1),b'\xab')
        with self.assertRaises(ValueError): decode_read(response,0,1)
        with self.assertRaises(ValueError): decode_read(response[:-1]+b'\x10',0x5a000,1)
        with self.assertRaises(ValueError): decode_read(PREFIX+b'\x01\x71\x05',0,1)

    def test_read_transport(self):
        sent=[]
        def receive(deadline):
            request=sent[-1]
            return PREFIX+b'\x71'+request[5:12]+bytes(128)
        self.assertEqual(read_song(sent.append,receive,slot=19,pacing=0),bytes(SIZE))
        self.assertEqual(len(sent),320); self.assertTrue(all(p[4]==0x70 for p in sent))
        sent=[]
        with self.assertRaises(TimeoutError): read_song(sent.append,lambda deadline:None,slot=19,pacing=0)
        self.assertEqual(len(sent),1)

if __name__=='__main__': unittest.main()
