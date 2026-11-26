// SPDX-License-Identifier: GPL-3.0-or-later
//! Lenient reading of saved songs: invalid stored values are repaired, with
//! one message per change. Songs without a 1.34..1.39 header are not guessed.
use crate::{restore,song,SONG_SIZE};

/// Firmware song_clear() image, written by tests/sanitize_test.c.in. The
/// firmware load-time repair uses the same defaults, so both repair alike.
const SONG_CLEAR:&[u8]=include_bytes!("../../../tests/fixtures/song-clear-1.39.bin");
const EMPTY:[u8;6]=[0,0,0,255,0,0];

fn defaults()->Result<Vec<u8>,String>{Ok(SONG_CLEAR.to_vec())}
fn kind_name(code:u8)->String{
 match code{0=>"Empty".into(),1=>"CCi".into(),2=>"Prb".into(),3=>"Euc".into(),4=>"Pre".into(),5=>"!Pr".into(),6=>"Hrm".into(),7=>"Inv".into(),8=>"Gli".into(),9=>"Scn".into(),0x90=>"Note".into(),0xb0=>"CC".into(),0xc0=>"PC".into(),c=>format!("unknown type {c}")}
}
/// Return the repaired image and the list of repairs (empty if none were needed).
pub fn repair(data:&[u8])->Result<(Vec<u8>,Vec<String>),String>{
 if !restore::header_valid(data){return Err("Source must be complete 1.34..1.39 SONG image (older songs: LOAD and SAVE on CARBON first)".into());}
 let defaults=defaults()?;if defaults.len()!=SONG_SIZE{return Err("Default template has wrong size".into());}
 let mut d=data.to_vec();let mut out=vec![];
 for i in 0..2304{let o=1824+i*6;
  if !restore::event_valid(d[0],&d[o..o+6]){
   let e=&d[o..o+6];
   out.push(format!("Track {} step {} slot {}: {} data {} {} {} -> Empty",i/384+1,(i/6)%64+1,i%6+1,kind_name(e[0]),e[1],e[2],e[3]));
   d[o..o+6].copy_from_slice(&EMPTY);}}
 out.extend(song::repair_fields(&mut d,&defaults));
 restore::validate_source(&d)?;song::validate(&d)?;
 Ok((d,out))
}
/// Problems that repair() would fix, without changing anything.
pub fn check(data:&[u8])->Vec<String>{
 match repair(data){Ok((_,r))=>r,Err(e)=>vec![e]}
}

#[cfg(test)]mod tests{
 use super::*;
 fn good()->Vec<u8>{defaults().unwrap()}
 #[test]fn valid_song_unchanged(){let d=good();let(r,msgs)=repair(&d).unwrap();assert_eq!(r,d);assert!(msgs.is_empty());}
 #[test]fn repairs_device_findings(){
  let mut d=good();
  // Note with pitch above 127 (shown as "D18" on CARBON), track 1 step 5 slot 1.
  let o=1824+(4*6)*6;d[o..o+6].copy_from_slice(&[0x90,230,100,255,24,0]);
  d[15663]=255;d[15665]=0;d[15664]=200;
  let o=48;d[o..o+6].copy_from_slice(&[3,0,0,0,0,0]);
  let(r,msgs)=repair(&d).unwrap();
  assert_eq!(msgs.len(),5,"{msgs:?}");
  assert!(msgs[0].starts_with("Track 1 step 5 slot 1: Note data 230"));
  assert!(msgs.iter().any(|m|m.starts_with("Setting scene_sync: 255 -> 0")));
  assert!(msgs.iter().any(|m|m.starts_with("Setting magic_chance: 0 -> 100")));
  assert!(msgs.iter().any(|m|m.starts_with("Setting magic_range: 200 -> 12")));
  assert!(msgs.iter().any(|m|m.starts_with("Song list entry 1")));
  assert_eq!(&r[o..o+1],&[255]);assert_eq!(&r[1824+144..1824+150],&EMPTY);
  assert!(restore::validate_source(&r).is_ok() && song::validate(&r).is_ok());
  assert_eq!(check(&d),msgs);
 }
 /// Same input, same bytes and count as the firmware (tests/sanitize_test.c.in).
 #[test]fn matches_firmware_fixtures(){
  let bad=include_bytes!("../../../tests/fixtures/invalid-song-1.39.bin");
  let want=include_bytes!("../../../tests/fixtures/invalid-song-1.39-repaired.bin");
  let(r,msgs)=repair(bad).unwrap();assert_eq!(msgs.len(),9,"{msgs:?}");assert!(r==want.to_vec());
  assert!(repair(SONG_CLEAR).unwrap().1.is_empty());
 }
 #[test]fn old_header_rejected(){let mut d=good();d[0]=30;assert!(repair(&d).is_err());assert!(repair(&[0;4]).is_err());}
}
