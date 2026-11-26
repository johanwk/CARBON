// SPDX-License-Identifier: GPL-3.0-or-later
//! Validated restore planning/execution. Every hardware write awaits ACK.
use crate::{hash, song_range, SONG_SIZE};
use serde_json::{json, Value};
const SECTOR: usize = 4096;
/// Explicit panel slot in the song bank; backup identity is mandatory.
pub fn validate_hardware_target(slot:usize,backup:&Value)->Result<(),String>{
 song_range(slot)?;
 if backup["slot"]!=slot {return Err("Hardware restore target must match the verified backup slot".into());}
 Ok(())
}

/// User assertion, not a device capability query. Checked before opening MIDI.
pub fn validate_hardware_source(data:&[u8],firmware:&str)->Result<(),String>{
    if !["--firmware-9c91ab0","--firmware-1.36-gli3","--firmware-community-1.36","--firmware-community-1.37","--firmware-community-1.38","--firmware-community-1.39"].contains(&firmware){return Err("Require --firmware-community-1.36, 1.37, 1.38 or 1.39 (see README)".into());}
    validate_source(data)?;
    let version=if firmware=="--firmware-9c91ab0"{35}else if firmware=="--firmware-community-1.39"{39}else if firmware=="--firmware-community-1.38"{38}else if firmware=="--firmware-community-1.37"{37}else{36};
    if data[..4]!=[version,0,1,0]{return Err("Source version must match asserted firmware; no implicit migration".into());}
    crate::song::validate(data)
}

fn nibble(value: usize,count:usize)->Vec<u8>{(0..count).rev().map(|i|((value>>(i*4))&15) as u8).collect()}
pub fn stage(offset:usize,data:&[u8])->Result<Vec<u8>,String>{
    if data.is_empty() || data.len()>64 || offset>SECTOR-data.len(){return Err("Invalid staging range".into());}
    let mut p=vec![0xf0,0,1,0x72,0x49,0x72];p.extend(nibble(offset,6));p.push(data.len() as u8);
    for &v in data {p.extend(nibble(v as usize,2));}p.push(0xf7);Ok(p)
}
pub fn commit(address:usize)->Result<Vec<u8>,String>{
    if address%SECTOR!=0 || address>64*SONG_SIZE-SECTOR {return Err("Invalid song-sector address".into());}
    let mut p=vec![0xf0,0,1,0x72,0x49,0x73];p.extend(nibble(address,6));p.extend(nibble(SECTOR,4));p.push(0xf7);Ok(p)
}
pub fn ack(packet:&[u8],cmd:u8)->Result<(),String>{
    if packet!=[0xf0,0,1,0x72,0x49,1,cmd,1,0xf7] {return Err(format!("Missing/malformed/error ACK for{cmd:02x}: {packet:02x?}"));}Ok(())
}
pub fn diff(before:&[u8],after:&[u8])->Result<Value,String>{
    if before.len()!=SONG_SIZE || after.len()!=SONG_SIZE {return Err("Wrong image size".into());}
    let changed:Vec<usize>=before.iter().zip(after).enumerate().filter_map(|(i,(a,b))|(a!=b).then_some(i)).collect();
    let mut events=vec![];
    for i in 0..2304 {let o=1824+i*6; if before[o..o+6]!=after[o..o+6]{
        let decode=|s:&[u8]|json!({"type":s[0],"data0":s[1],"data1":s[2],"padding":s[3],"length_ticks":u16::from_le_bytes([s[4],s[5]])});
        events.push(json!({"track":i/384+1,"step":(i/6)%64+1,"slot":i%6+1,"before":decode(&before[o..o+6]),"after":decode(&after[o..o+6])}));
    }}
    Ok(json!({"event_changes":events,"changed_byte_offsets":changed,
      "unmodelled_changes":changed.iter().filter(|i| !(1824usize..15648).contains(*i)).collect::<Vec<_>>(),
      "warning":"Stored fields decoded below; event indices/raw bytes retained; opaque changes identified"}))
}
/// One stored event (6 bytes) is valid for song format version 1.`version`.
pub fn event_valid(version:u8,e:&[u8])->bool{
    match e[0] {
        0|4|5=>true,
        1=>((16..=29).contains(&e[1]) && e[2]<=127) || (version>=36 && ((e[1]==34 && e[2]<=8)||(e[1]==35 && e[2]<=127))),
        8=>version>=36 && e[1]<=8 && e[2]<=127,
        9=>version>=37 && e[1]<=63 && e[2]==0,
        2=>e[1]<=100,3=>e[1]<=27 && e[2]<=7,
        6=>e[1]<=16 && e[2]<=13,7=>e[1]<=5 && e[2]<=1,
        0x90=>e[1]<=127 && e[2]<=127 && (version==34 || e[3]<=103 || e[3]==255),
        0xb0=>e[1]<=127 && e[2]<=127,0xc0=>e[1]<=127,_=>false
    }
}
/// Complete 1.34..1.39 SONG image header and trailer (1.39 = 1.38 format).
pub fn header_valid(data:&[u8])->bool{
    data.len()==SONG_SIZE && [34,35,36,37,38,39].contains(&data[0]) && data[1..4]==[0,1,0] && data[SONG_SIZE-4..]==[0x47,0x4e,0x4f,0x53]
}
/// Validate implemented event encodings. Unused bytes remain opaque.
pub fn validate_source(data:&[u8])->Result<(),String>{
    if !header_valid(data){return Err("Source must be complete 1.34..1.39 SONG image".into());}
    for (i,e) in data[1824..15648].chunks_exact(6).enumerate(){
        if !event_valid(data[0],e){return Err(format!("Invalid event: track{} step{} slot{}",i/384+1,(i/6)%64+1,i%6+1));}
    }
    Ok(())
}
#[derive(Clone,Copy)] pub enum Mode {Replace,Patch}
pub struct Plan {slot:usize,before:Vec<u8>,after:Vec<u8>,sectors:Vec<usize>, pub preview:Value,mode:Mode}
impl Plan {
    pub fn new(slot:usize,before:Vec<u8>,after:Vec<u8>,mode:Mode)->Result<Self,String>{
        song_range(slot)?;let mut preview=diff(&before,&after)?;
        preview["stored_fields"]=crate::song::diff(&before,&after)?;
        // Initial restore scope: identified current images, not blank/legacy recovery.
        validate_source(&after)?;
        crate::song::validate(&after)?;
        if matches!(mode,Mode::Patch) && before.iter().zip(&after).enumerate().any(|(i,(a,b))|a!=b && !(1824..15648).contains(&i)){
            return Err("Patch currently permits only shared event changes; global/scene semantics unmodelled".into());
        }
        let sectors=(0..5).filter(|i|matches!(mode,Mode::Replace)||before[i*SECTOR..(i+1)*SECTOR]!=after[i*SECTOR..(i+1)*SECTOR]).collect();
        Ok(Self {slot,before,after,sectors,preview,mode})
    }
    pub fn summary(&self)->Value{json!({"target_slot":self.slot,"base_sha256":hash(&self.before),"result_sha256":hash(&self.after),
        "sector_indices":self.sectors,"approval_token":self.approval_token(),"diff":self.preview,
        "limitations":"Plan preview only; execution requires explicit approval. Reserved/padding bytes preserved; replacement is not atomic."})}
    /// Short human preview; full semantic/raw changes remain in summary().
    pub fn brief(&self)->String {
        let event_count=self.preview["event_changes"].as_array().map_or(0,Vec::len);
        let fields=self.preview["stored_fields"]["field_changes"].as_array().map_or(0,Vec::len);
        let opaque=self.preview["stored_fields"]["opaque_changes"].as_array().map_or(0,Vec::len);
        let notes=self.after[1824..15648].chunks_exact(6).filter(|e|e[0]==0x90).count();
        format!("Target: song {} - {}\nResult: firmware song format 1.{}, {} stored Notes\nChanges: {} event slots, {} stored fields, {} opaque bytes\nWrites: {} complete sectors; other song slots untouched\nBase SHA256: {}\nResult SHA256: {}\nA fresh matching backup and a new recovery file are required before writing.\nReplacement is not atomic; keep CARBON stopped and connected.",
            self.slot,if matches!(self.mode,Mode::Replace){"REPLACE entire song"}else{"patch changed sectors"},self.after[0],notes,event_count,fields,opaque,self.sectors.len(),hash(&self.before),hash(&self.after))
    }
    pub fn confirmation_phrase(&self)->String {format!("replace {}",self.slot)}
    pub fn accepts_confirmation(&self,line:&str)->bool {line.trim_end_matches(&['\r','\n'][..])==self.confirmation_phrase()}
    pub fn approval_token(&self)->String {format!("slot{}:{}:{}:{}",self.slot,match self.mode{Mode::Replace=>"replace",Mode::Patch=>"patch"},hash(&self.before),hash(&self.after))}
}
/// Adapter must enforce finite response/read deadlines, stopped transport and
/// verified hardened firmware. Errors abort; never automatically retry writes.
pub trait Device {fn read_song(&mut self,slot:usize)->Result<Vec<u8>,String>;fn exchange(&mut self,packet:&[u8])->Result<Vec<u8>,String>;}
/// save_backup MUST durably save the fresh target image before returning Ok.
/// Caller approval token binds explicit target/base/result after showing preview.
pub fn execute<D:Device,F:FnMut(&[u8])->Result<(),String>>(device:&mut D,plan:&Plan,approval:&str,stopped:bool,hardened:bool,mut save_backup:F)->Result<(),String>{
    if !stopped || !hardened || approval!=plan.approval_token(){return Err("Missing stopped/firmware/exact-plan approval".into());}
    let first=device.read_song(plan.slot)?;let second=device.read_song(plan.slot)?;
    if first!=second || first!=plan.before {return Err("Fresh target differs: abort and re-plan".into());}
    save_backup(&first)?;
    let base=song_range(plan.slot)?.start;
    for &sector in &plan.sectors {
        let bytes=&plan.after[sector*SECTOR..(sector+1)*SECTOR];
        for offset in (0..SECTOR).step_by(64){ack(&device.exchange(&stage(offset,&bytes[offset..offset+64])?)?,0x72)?;}
        ack(&device.exchange(&commit(base+sector*SECTOR)?)?,0x73)?;
    }
    if device.read_song(plan.slot)?!=plan.after {return Err("READBACK MISMATCH: target may be damaged; retain backup".into());}
    Ok(())
}
#[cfg(test)] mod tests {
 use super::*;
 struct Fake {data:Vec<u8>,buf:Vec<u8>,writes:usize,fail:bool}
 impl Device for Fake {fn read_song(&mut self,_:usize)->Result<Vec<u8>,String>{Ok(self.data.clone())}
 fn exchange(&mut self,p:&[u8])->Result<Vec<u8>,String>{self.writes+=1;if self.fail{return Err("timeout".into());}
 let number=|s:&[u8]|s.iter().fold(0usize,|a,v|(a<<4)|*v as usize);
 if p[5]==0x72 {let o=number(&p[6..12]);for(i,n)in p[13..p.len()-1].chunks_exact(2).enumerate(){self.buf[o+i]=(n[0]<<4)|n[1];}}
 else {let addr=number(&p[6..12]);let o=addr%SONG_SIZE;self.data[o..o+SECTOR].copy_from_slice(&self.buf);}
 Ok(vec![0xf0,0,1,0x72,0x49,1,p[5],1,0xf7])}}
 fn fixture()->Vec<u8>{let mut d=vec![0;SONG_SIZE];d[4..8].copy_from_slice(&120f32.to_le_bytes());d[8]=50;d[12]=2;
 for i in 0..64{d[48+6*i]=255;}for s in 0..36{let o=480+16*s;d[o+3]=255;d[o+5]=64;d[o+6]=128;d[o+12]=12;}
 for s in 0..384{d[1056+2*s+1]=1;}d[15649]=100;d[15650]=255;d[15665]=100;d[15810]=255;for i in 0..6{d[15811+i]=254;}d}
 fn setup()->(Plan,Fake){let mut before=fixture();before[..4].copy_from_slice(&[34,0,1,0]);before[SONG_SIZE-4..].copy_from_slice(&[0x47,0x4e,0x4f,0x53]);let mut after=before.clone();after[1830]=0x90;after[1831]=48;after[1832]=97;let plan=Plan::new(20,before.clone(),after,Mode::Patch).unwrap();(plan,Fake{data:before,buf:vec![0;SECTOR],writes:0,fail:false})}
 #[test] fn brief_and_confirmation(){
  let (p,_)=setup();let p=Plan::new(37,p.before,p.after,Mode::Replace).unwrap();
  assert!(p.brief().contains("song 37"));assert!(p.brief().contains("5 complete sectors"));assert!(p.brief().lines().count()<12);
  assert!(p.accepts_confirmation("replace 37\n"));assert!(p.accepts_confirmation("replace 37\r\n"));
  for s in ["yes","replace 30",""," replace 37","replace 37 extra"]{assert!(!p.accepts_confirmation(s));}
 }
 #[test] fn hardware_target_identity(){
  for slot in 1..=64{assert!(validate_hardware_target(slot,&json!({"slot":slot})).is_ok());}
  for slot in [0,65,usize::MAX]{assert!(validate_hardware_target(slot,&json!({"slot":slot})).is_err());}
  assert!(validate_hardware_target(31,&json!({"slot":30})).is_err());
  assert!(validate_hardware_target(31,&json!({})).is_err());
 }
 #[test] fn note_delay_hardware_guard(){
  let(p,_)=setup();
  assert!(validate_hardware_source(&p.after,"--firmware-9c91ab0").is_err());
  let mut modern=p.after.clone();modern[..4].copy_from_slice(&[35,0,1,0]);modern[1833]=99;modern[1056]=101;
  assert!(validate_hardware_source(&modern,"--firmware-9c91ab0").is_ok());
  assert!(validate_hardware_source(&modern,"--firmware-0623fee").is_err());
  assert!(validate_hardware_source(&[],"--firmware-9c91ab0").is_err());
  modern[1833]=104;assert!(validate_hardware_source(&modern,"--firmware-9c91ab0").is_err());
 }
 #[test] fn modern_replacement_preserves_delays(){
  let(p,mut device)=setup();let mut modern=p.after.clone();modern[..4].copy_from_slice(&[35,0,1,0]);
  modern[1833]=99;modern[1056]=101;
  validate_hardware_source(&modern,"--firmware-9c91ab0").unwrap();
  let plan=Plan::new(30,p.before,modern.clone(),Mode::Replace).unwrap();
  execute(&mut device,&plan,&plan.approval_token(),true,true,|_|Ok(())).unwrap();
  assert_eq!(device.writes,325);assert_eq!(device.data,modern);
 }
 #[test] fn patch_and_replace(){let(p,mut d)=setup();let mut backed=false;execute(&mut d,&p,&p.approval_token(),true,true,|_|{backed=true;Ok(())}).unwrap();assert!(backed);assert_eq!(d.writes,65);assert_eq!(d.data,p.after);let(p,mut d)=setup();let p=Plan::new(20,p.before,p.after,Mode::Replace).unwrap();execute(&mut d,&p,&p.approval_token(),true,true,|_|Ok(())).unwrap();assert_eq!(d.writes,325);}
 #[test] fn rejects_before_write(){let(p,mut d)=setup();assert!(execute(&mut d,&p,"wrong",true,true,|_|Ok(())).is_err());assert_eq!(d.writes,0);d.data[100]=1;assert!(execute(&mut d,&p,&p.approval_token(),true,true,|_|Ok(())).is_err());assert_eq!(d.writes,0);let(p,mut d)=setup();assert!(execute(&mut d,&p,&p.approval_token(),true,true,|_|Err("disk full".into())).is_err());assert_eq!(d.writes,0);}
 #[test] fn abort_no_retry(){let(p,mut d)=setup();d.fail=true;assert!(execute(&mut d,&p,&p.approval_token(),true,true,|_|Ok(())).is_err());assert_eq!(d.writes,1);assert!(commit(1).is_err());assert!(stage(4095,&[0,1]).is_err());assert!(ack(&[0;9],0x73).is_err());}

 struct Fault {inner:Fake,at:usize,reads:usize,bad_readback:bool}
 impl Device for Fault{
 fn read_song(&mut self,s:usize)->Result<Vec<u8>,String>{self.reads+=1;let mut d=self.inner.read_song(s)?;if self.bad_readback && self.reads==3{d[100]^=1;}Ok(d)}
 fn exchange(&mut self,p:&[u8])->Result<Vec<u8>,String>{if self.inner.writes+1==self.at{self.inner.writes+=1;return Ok(vec![0xf0,0,1,0x72,0x49,1,p[5],5,0xf7]);}self.inner.exchange(p)}}
 #[test] fn interruptions_every_phase(){for at in 1..=325{let(p,d)=setup();let p=Plan::new(20,p.before,p.after,Mode::Replace).unwrap();let mut d=Fault{inner:d,at,reads:0,bad_readback:false};assert!(execute(&mut d,&p,&p.approval_token(),true,true,|_|Ok(())).is_err());assert_eq!(d.inner.writes,at);}}
 #[test] fn readback_failure(){let(p,d)=setup();let mut d=Fault{inner:d,at:0,reads:0,bad_readback:true};let err=execute(&mut d,&p,&p.approval_token(),true,true,|_|Ok(())).unwrap_err();assert!(err.contains("READBACK"));assert_eq!(d.inner.writes,65);}
 #[test] fn source_and_patch_validation(){let(p,_)=setup();let mut bad=p.after.clone();bad[1830]=99;assert!(validate_source(&bad).is_err());bad=p.after.clone();bad[100]^=1;assert!(Plan::new(20,p.before.clone(),bad,Mode::Patch).is_err());assert!(validate_source(&[0;4]).is_err());}

 #[test] fn validation_boundaries_and_mode(){let(p,_)=setup();let q=Plan::new(20,p.before.clone(),p.after.clone(),Mode::Replace).unwrap();assert_ne!(p.approval_token(),q.approval_token());for(o,v)in[(8,49),(480,23),(481,16),(485,0),(1057,0),(15738,101),(15810,12)]{let mut bad=p.after.clone();bad[o]=v;assert!(Plan::new(20,p.before.clone(),bad,Mode::Replace).is_err(),"offset{o}");}let mut bad=p.after.clone();bad[4..8].copy_from_slice(&f32::NAN.to_le_bytes());assert!(Plan::new(20,p.before,bad,Mode::Replace).is_err());}
}
