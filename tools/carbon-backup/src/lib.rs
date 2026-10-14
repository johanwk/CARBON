pub mod midi;
pub mod song;
pub mod transfer;
pub mod restore;
// SPDX-License-Identifier: GPL-3.0-or-later
use sha2::{Digest, Sha256};
use serde_json::{json, Value};
use std::io::{Read, Write};
pub const SONG_SIZE: usize = 0x5000;
pub const BANK_SIZE: usize = 64 * SONG_SIZE;
const MAGIC: &[u8;8] = b"CARBNBK1";
pub fn hash(data: &[u8]) -> String { format!("{:x}", Sha256::digest(data)) }
pub fn request(address: usize) -> Result<Vec<u8>, String> {
    if address % 64 != 0 || address > BANK_SIZE - 64 { return Err("Read outside song bank".into()); }
    let mut p=vec![0xf0,0,1,0x72,0x49,0x70];
    for shift in (0..6).rev() { p.push(((address >> (shift*4)) & 15) as u8); }
    p.extend([64,0xf7]); Ok(p)
}
pub fn response(p: &[u8], address: usize) -> Result<Option<Vec<u8>>, String> {
    if !p.starts_with(&[0xf0,0,1,0x72,0x49]) { return Ok(None); }
    if p.get(5)==Some(&1) { return Err(format!("Device error packet: {p:02x?}")); }
    if p.get(5)!=Some(&0x71) { return Ok(None); }
    if p.len()!=142 || p[12]!=64 || p[141]!=0xf7 { return Err("Bad reply length/framing".into()); }
    if p[6..12].iter().chain(p[13..141].iter()).any(|v| *v>15) {return Err("Noncanonical nibble".into());}
    let a=p[6..12].iter().fold(0usize,|a,v| (a<<4)|usize::from(*v));
    if a!=address { return Err(format!("Reply address mismatch: {a:06x} != {address:06x}")); }
    Ok(Some(p[13..141].chunks_exact(2).map(|n| (n[0]<<4)|n[1]).collect()))
}
pub fn manifest(bank: &[u8], input: &str, output: &str, timestamp: u64) -> Result<Value,String> {
    if bank.len()!=BANK_SIZE { return Err("Wrong bank length".into()); }
    Ok(json!({"format":"CARBNBK1","schema":1,"timestamp_unix":timestamp,
      "input_port":input,"output_port":output,"matching_capture_passes":2,
      "scope":"64 saved songs only; no configuration, firmware or unsaved RAM",
      "bank_bytes":BANK_SIZE,"bank_sha256":hash(bank),
      "slots":bank.chunks_exact(SONG_SIZE).enumerate().map(|(i,s)|json!({
          "slot":i+1,"address":i*SONG_SIZE,"bytes":SONG_SIZE,"sha256":hash(s),
          "song_magic_valid":s[SONG_SIZE-4..]==[0x47,0x4e,0x4f,0x53]})).collect::<Vec<_>>() }))
}
pub fn write_archive<W:Write>(mut w:W, bank:&[u8], m:&Value) -> Result<(),String> {
    if bank.len()!=BANK_SIZE { return Err("Wrong bank length".into()); }
    let meta=serde_json::to_vec(m).map_err(|e|e.to_string())?;
    w.write_all(MAGIC).and_then(|_|w.write_all(&(meta.len() as u32).to_le_bytes()))
        .and_then(|_|w.write_all(&meta)).and_then(|_|w.write_all(bank)).map_err(|e|e.to_string())
}
pub fn verify_archive<R:Read>(mut r:R) -> Result<(Value,Vec<u8>),String> {
    let mut header=[0u8;12]; r.read_exact(&mut header).map_err(|e|e.to_string())?;
    if &header[..8]!=MAGIC { return Err("Unknown archive format".into()); }
    let n=u32::from_le_bytes(header[8..12].try_into().unwrap()) as usize;
    if n>1_000_000 {return Err("Manifest too large".into());}
    let mut meta=vec![0;n]; r.read_exact(&mut meta).map_err(|e|e.to_string())?;
    let m:Value=serde_json::from_slice(&meta).map_err(|e|e.to_string())?;
    let mut bank=vec![0;BANK_SIZE]; r.read_exact(&mut bank).map_err(|e|e.to_string())?;
    let mut extra=[0]; if r.read(&mut extra).map_err(|e|e.to_string())?!=0 {return Err("Trailing archive data".into());}
    if m["format"]!="CARBNBK1" || m["schema"]!=1 || m["bank_bytes"]!=BANK_SIZE
        || m["bank_sha256"]!=hash(&bank) || m["matching_capture_passes"]!=2 { return Err("Manifest/bank mismatch".into()); }
    let slots=m["slots"].as_array().ok_or("Missing slots")?;
    if slots.len()!=64 {return Err("Wrong slot count".into());}
    for (i,s) in bank.chunks_exact(SONG_SIZE).enumerate() {
        if slots[i]["slot"]!=i+1 || slots[i]["address"]!=i*SONG_SIZE
            || slots[i]["bytes"]!=SONG_SIZE || slots[i]["sha256"]!=hash(s)
            || slots[i]["song_magic_valid"]!=(s[SONG_SIZE-4..]==[0x47,0x4e,0x4f,0x53]) {return Err(format!("Slot{} mismatch",i+1));}
    }
    Ok((m,bank))
}
#[cfg(test)] mod tests {
    use super::*;
    #[test] fn bounds(){assert!(request(0).is_ok());assert!(request(BANK_SIZE-64).is_ok());assert!(request(BANK_SIZE).is_err());assert!(request(1).is_err());assert!(request(usize::MAX).is_err());}
    #[test] fn packets(){let q=request(0x5a000).unwrap();assert_eq!(q.len(),14);assert_eq!(q[5],0x70);let mut p=q[..13].to_vec();p[5]=0x71;p.extend([10,11].repeat(64));p.push(0xf7);assert_eq!(response(&p,0x5a000).unwrap().unwrap(),vec![0xab;64]);assert!(response(&p,0).is_err());p[13]=16;assert!(response(&p,0x5a000).is_err());}
    #[test] fn archive_roundtrip(){let bank=vec![255;BANK_SIZE];let m=manifest(&bank,"in","out",0).unwrap();let mut a=vec![];write_archive(&mut a,&bank,&m).unwrap();assert_eq!(verify_archive(&a[..]).unwrap().1,bank);let last=a.len()-1;a[last]^=1;assert!(verify_archive(&a[..]).is_err());}
    #[test] fn truncated(){assert!(verify_archive(&b"bad"[..]).is_err());}
}

// Single-song archives retain original panel-slot identity; no magic repair.
pub fn song_range(slot: usize) -> Result<std::ops::Range<usize>, String> {
    if !(1..=64).contains(&slot) {return Err("Choose panel slot1..64".into());}
    Ok((slot-1)*SONG_SIZE..slot*SONG_SIZE)
}
pub fn song_manifest(data:&[u8],slot:usize,input:&str,output:&str,timestamp:u64)->Result<Value,String>{
    let range=song_range(slot)?;
    if data.len()!=SONG_SIZE {return Err("Wrong song length".into());}
    Ok(json!({"format":"CARBNSG1","schema":1,"slot":slot,"address":range.start,
      "song_bytes":SONG_SIZE,"song_sha256":hash(data),"matching_capture_passes":2,
      "timestamp_unix":timestamp,"input_port":input,"output_port":output,
      "scope":"one saved raw song; no configuration, firmware or unsaved RAM",
      "song_magic_valid":data[SONG_SIZE-4..]==[0x47,0x4e,0x4f,0x53]}))
}
pub fn write_song_archive<W:Write>(mut w:W,data:&[u8],m:&Value)->Result<(),String>{
    if data.len()!=SONG_SIZE {return Err("Wrong song length".into());}
    let meta=serde_json::to_vec(m).map_err(|e|e.to_string())?;
    w.write_all(b"CARBNSG1").and_then(|_|w.write_all(&(meta.len() as u32).to_le_bytes()))
      .and_then(|_|w.write_all(&meta)).and_then(|_|w.write_all(data)).map_err(|e|e.to_string())
}
pub fn verify_song_archive<R:Read>(mut r:R)->Result<(Value,Vec<u8>),String>{
    let mut header=[0u8;12];r.read_exact(&mut header).map_err(|e|e.to_string())?;
    if &header[..8]!=b"CARBNSG1" {return Err("Unknown song archive format".into());}
    let n=u32::from_le_bytes(header[8..12].try_into().unwrap()) as usize;
    if n>1_000_000 {return Err("Manifest too large".into());}
    let mut meta=vec![0;n];r.read_exact(&mut meta).map_err(|e|e.to_string())?;
    let m:Value=serde_json::from_slice(&meta).map_err(|e|e.to_string())?;
    let slot=m["slot"].as_u64().and_then(|v|usize::try_from(v).ok()).ok_or("Invalid slot")?;
    let range=song_range(slot)?;
    let mut data=vec![0;SONG_SIZE];r.read_exact(&mut data).map_err(|e|e.to_string())?;
    let mut extra=[0];if r.read(&mut extra).map_err(|e|e.to_string())?!=0 {return Err("Trailing archive data".into());}
    if m["format"]!="CARBNSG1" || m["schema"]!=1 || m["address"]!=range.start
      || m["song_bytes"]!=SONG_SIZE || m["matching_capture_passes"]!=2
      || m["song_sha256"]!=hash(&data)
      || m["song_magic_valid"]!=(data[SONG_SIZE-4..]==[0x47,0x4e,0x4f,0x53]) {return Err("Song manifest/data mismatch".into());}
    Ok((m,data))
}
#[cfg(test)] mod song_tests {
    use super::*;
    #[test] fn slot_bounds(){assert!(song_range(0).is_err());assert!(song_range(65).is_err());assert_eq!(song_range(19).unwrap(),0x5a000..0x5f000);assert_eq!(song_range(64).unwrap().end,BANK_SIZE);}
    #[test] fn raw_song_roundtrip(){let data=vec![255;SONG_SIZE];let m=song_manifest(&data,19,"in","out",0).unwrap();let mut a=vec![];write_song_archive(&mut a,&data,&m).unwrap();assert_eq!(verify_song_archive(&a[..]).unwrap().1,data);assert!(verify_song_archive(&a[..a.len()-1]).is_err());let mut extra=a.clone();extra.push(0);assert!(verify_song_archive(&extra[..]).is_err());let last=a.len()-1;a[last]^=1;assert!(verify_song_archive(&a[..]).is_err());}
}
