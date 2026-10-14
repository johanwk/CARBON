# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline inspect/diff/event patch. No device access."""
import argparse
import json
from pathlib import Path
from .codec import SongImage, semantic_diff
from .model import TYPES

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='command',required=True)
    inspect=sub.add_parser('inspect'); inspect.add_argument('image',type=Path)
    inspect.add_argument('--track',type=int,default=1); inspect.add_argument('--step',type=int,default=1)
    diff=sub.add_parser('diff'); diff.add_argument('before',type=Path); diff.add_argument('after',type=Path)
    patch=sub.add_parser('patch-event')
    patch.add_argument('image',type=Path); patch.add_argument('output',type=Path)
    patch.add_argument('--base-sha256',required=True)
    for name in ('track','step','slot'): patch.add_argument('--'+name,type=int,required=True)
    for name in ('type','data0','data1','length'): patch.add_argument('--'+name,type=int)
    args=parser.parse_args()
    try:
        if args.command=='diff':
            result=semantic_diff(SongImage(args.before.read_bytes()),SongImage(args.after.read_bytes()))
        else:
            song=SongImage(args.image.read_bytes())
            if args.command=='inspect':
                result={'version':f'{song.version>>16}.{song.version&65535}',
                        'sha256':song.sha256,'track':args.track,'step':args.step,
                        'slots':[dict(slot=p,name=TYPES.get(song.event(args.track,args.step,p).type,'Unknown'),
                                      **song.event(args.track,args.step,p).__dict__) for p in range(1,7)]}
            else:
                fields={name:getattr(args,name) for name in ('type','data0','data1','length')
                        if getattr(args,name) is not None}
                updated=song.patch_event(args.track,args.step,args.slot,base_sha256=args.base_sha256,**fields)
                result=semantic_diff(song,updated)
                # Exclusive creation: never overwrite the original/another backup.
                with args.output.open('xb') as output: output.write(updated.encode())
        print(json.dumps(result,indent=2))
    except (ValueError,OSError) as error:
        parser.error(str(error))

if __name__=='__main__': main()
