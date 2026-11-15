// SPDX-License-Identifier: GPL-3.0-or-later
//! Simple JSON song transfers. Consent is required before any device access.
use crate::{document,hash,restore::{self,Device,Mode,Plan},song_range};
use serde_json::Value;

pub const PORT:&str="CARBON Port 1";

/// Return (upload, slot, JSON path); retain the original short command forms.
pub fn arguments(args:&[String])->Result<(bool,usize,String),String>{
    let usage="Usage: backup song SLOT FILE.json | upload song SLOT FILE.json (also: download SLOT FILE.json | upload FILE.json SLOT)";
    let (upload,slot,path)=match args {
        [cmd,song,slot,path] if (cmd=="backup" || cmd=="upload") && song=="song" => (cmd=="upload",slot,path),
        [cmd,slot,path] if cmd=="download" => (false,slot,path),
        [cmd,path,slot] if cmd=="upload" => (true,slot,path),
        _=>return Err(usage.into()),
    };
    let slot=slot.parse::<usize>().map_err(|_|"Song number must be 1..64")?;
    song_range(slot)?;
    if path.is_empty(){return Err("JSON filename required".into());}
    Ok((upload,slot,path.clone()))
}


pub fn firmware_assertion(raw:&[u8])->Result<&'static str,String>{
 match raw.first(){Some(36)=>Ok("--firmware-community-1.36"),Some(37)=>Ok("--firmware-community-1.37"),_=>Err("Simple upload requires a 1.36 or 1.37 source".into())}
}
pub fn firmware_description(raw:&[u8])->Result<&'static str,String>{
 match firmware_assertion(raw)? {"--firmware-community-1.37"=>Ok("1.37-jwk-scn1 (scene gates)"),_=>Ok("1.36-jwk-rc1 (or compatible 1.36-jwk-snp3)")}
}
pub fn validate_upload(text:&str)->Result<Vec<u8>,String>{
    let doc=crate::json::parse(text)?;
    let (_,raw)=document::import(&doc)?;
    // The simple UI includes this firmware assertion in its confirmation text.
    // It is not inferred from stored song versions and is not a capability query.
    restore::validate_hardware_source(&raw,firmware_assertion(&raw)?)?;
    Ok(raw)
}
pub fn confirmation(action:&str,slot:usize,line:&str)->bool{
    line.trim_end_matches(&['\r','\n'][..])==format!("{action} {slot}")
}
fn read_pair<D:Device>(device:&mut D,slot:usize)->Result<Vec<u8>,String>{
    song_range(slot)?;
    let a=device.read_song(slot)?;let b=device.read_song(slot)?;
    if a.len()!=crate::SONG_SIZE || a!=b{return Err("Two saved-song reads did not match; aborted without writing".into());}
    Ok(a)
}
pub fn download<D:Device>(device:&mut D,slot:usize,approved:bool)->Result<Value,String>{
    if !approved{return Err("Cancelled".into());}
    let raw=read_pair(device,slot)?;
    document::export(&raw,slot)
}
/// The caller obtained approval for this target and compiled source before
/// connecting. Base bytes are backed up automatically, then rebound/rechecked by
/// the existing exact-plan executor before any staging commands are sent.
pub fn upload<D:Device,F:FnMut(&[u8])->Result<(),String>>(device:&mut D,slot:usize,raw:Vec<u8>,approved:bool,save_backup:F)->Result<String,String>{
    if !approved{return Err("Cancelled".into());}
    song_range(slot)?;
    restore::validate_hardware_source(&raw,firmware_assertion(&raw)?)?;
    let result=hash(&raw);
    let before=read_pair(device,slot)?;
    let plan=Plan::new(slot,before,raw,Mode::Replace)?;
    restore::execute(device,&plan,&plan.approval_token(),true,true,save_backup)?;
    Ok(result)
}

#[cfg(test)]mod tests{
    use super::*;
    struct Fake{data:Vec<u8>,stage:Vec<u8>,reads:usize,writes:usize,stale:bool,backed:std::rc::Rc<std::cell::Cell<bool>>}
    impl Device for Fake{
        fn read_song(&mut self,slot:usize)->Result<Vec<u8>,String>{
            assert_eq!(slot,37);self.reads+=1;
            let mut raw=self.data.clone();if self.stale && self.reads==3{raw[16000]^=1;}Ok(raw)
        }
        fn exchange(&mut self,p:&[u8])->Result<Vec<u8>,String>{
            assert!(self.backed.get(),"write before durable backup");self.writes+=1;
            let num=|xs:&[u8]|xs.iter().fold(0usize,|a,v|(a<<4)|*v as usize);
            if p[5]==0x72{let o=num(&p[6..12]);for(i,n)in p[13..p.len()-1].chunks_exact(2).enumerate(){self.stage[o+i]=(n[0]<<4)|n[1];}}
            else{let address=num(&p[6..12]);let range=song_range(37).unwrap();assert!(range.contains(&address));let o=address-range.start;self.data[o..o+4096].copy_from_slice(&self.stage);}
            Ok(vec![0xf0,0,1,0x72,0x49,1,p[5],1,0xf7])
        }
    }
    fn fixture()->Vec<u8>{let doc=crate::json::parse(include_str!("../templates/default-1.36.json")).unwrap();document::import(&doc).unwrap().1}
    fn device()->Fake{Fake{data:fixture(),stage:vec![0;4096],reads:0,writes:0,stale:false,backed:std::rc::Rc::new(std::cell::Cell::new(false))}}
    #[test]fn download_two_reads_no_writes(){let mut d=device();let doc=download(&mut d,37,true).unwrap();assert_eq!(doc["provenance"]["capture_slot"],37);assert_eq!(document::import(&doc).unwrap().1,d.data);assert_eq!(d.reads,2);assert_eq!(d.writes,0);}
    #[test]fn upload_json_and_readback(){let mut d=device();let mut raw=fixture();raw[4..8].copy_from_slice(&123f32.to_le_bytes());let mut saved=vec![];let backed=d.backed.clone();let result=upload(&mut d,37,raw.clone(),true,|b|{saved=b.to_vec();backed.set(true);Ok(())}).unwrap();assert_eq!(saved,fixture());assert_eq!(result,hash(&raw));assert_eq!(d.data,raw);assert_eq!(d.reads,5);assert_eq!(d.writes,325);}
    #[test]fn cancellation_invalid_stale_and_backup_failure(){
        let mut d=device();assert!(download(&mut d,37,false).is_err());assert!(upload(&mut d,37,fixture(),false,|_|Ok(())).is_err());assert_eq!(d.reads,0);
        assert!(upload(&mut d,65,fixture(),true,|_|Ok(())).is_err());assert_eq!(d.reads,0);
        let mut old=fixture();old[0]=34;assert!(upload(&mut d,37,old,true,|_|Ok(())).is_err());assert_eq!(d.reads,0);
        assert!(upload(&mut d,37,fixture(),true,|_|Err("disk full".into())).is_err());assert_eq!(d.writes,0);
        d.stale=true;d.reads=0;assert!(upload(&mut d,37,fixture(),true,|_|panic!("no backup after stale base")).is_err());assert_eq!(d.writes,0);
    }
    #[test]fn friendly_command_forms(){
        let parse=|xs:&[&str]|arguments(&xs.iter().map(|s|s.to_string()).collect::<Vec<_>>());
        for (cmd,write) in [("backup",false),("upload",true)] {
            assert_eq!(parse(&[cmd,"song","37","my song.json"]).unwrap(),(write,37,"my song.json".into()));
            for slot in ["0","65","abc"]{assert!(parse(&[cmd,"song",slot,"x.json"]).is_err());}
        }
        assert_eq!(parse(&["download","37","x.json"]).unwrap(),(false,37,"x.json".into()));
        assert_eq!(parse(&["upload","x.json","37"]).unwrap(),(true,37,"x.json".into()));
        for xs in [vec!["backup","song","37"],vec!["upload","song","37","x.json","extra"],vec!["backup","track","37","x.json"]]{assert!(parse(&xs).is_err());}
    }
    #[test]fn scene_gate_transfer(){
 let mut raw=fixture();raw[0]=37;raw[1824..1830].copy_from_slice(&[9,37,0,255,0,0]);
 let doc=document::export(&raw,37).unwrap();assert_eq!(validate_upload(&doc.to_string()).unwrap(),raw);
 assert!(firmware_description(&raw).unwrap().contains("1.37-jwk-scn1"));
 assert!(firmware_description(&fixture()).unwrap().contains("1.36-jwk-rc1"));
 let mut d=device();let backed=d.backed.clone();upload(&mut d,37,raw.clone(),true,|before|{assert_eq!(before,fixture());backed.set(true);Ok(())}).unwrap();
 assert_eq!(d.data,raw);assert_eq!(d.writes,325);
 assert_eq!(document::import(&download(&mut d,37,true).unwrap()).unwrap().1,raw);
 let mut d=device();assert!(upload(&mut d,37,raw.clone(),false,|_|panic!()).is_err());assert_eq!(d.reads,0);
 assert!(upload(&mut d,37,raw.clone(),true,|_|Err("disk full".into())).is_err());assert_eq!(d.writes,0);
 d.stale=true;d.reads=0;assert!(upload(&mut d,37,raw,true,|_|panic!()).is_err());assert_eq!(d.writes,0);
}
    #[test]fn community_source_guards(){
 let mut raw=fixture();
 raw[1824..1830].copy_from_slice(&[8,4,64,255,0,0]);
 raw[1830..1836].copy_from_slice(&[1,35,100,255,0,0]);
 let doc=document::export(&raw,37).unwrap();
 assert_eq!(validate_upload(&serde_json::to_string(&doc).unwrap()).unwrap(),raw);
 let mut d=device();let backed=d.backed.clone();
 upload(&mut d,37,raw.clone(),true,|_|{backed.set(true);Ok(())}).unwrap();
 assert_eq!(d.data,raw);assert_eq!(d.writes,325);
 assert!(validate_upload(include_str!("../templates/default-1.35.json")).is_err());
 for version in [34,35,37]{raw[0]=version;assert!(restore::validate_hardware_source(&raw,"--firmware-community-1.36").is_err());}
 assert!(restore::validate_hardware_source(&[],"--firmware-community-1.36").is_err());
}
 #[test]fn consent_and_source(){for action in ["download","replace"]{assert!(confirmation(action,37,&format!("{action} 37\n")));assert!(!confirmation(action,37,"yes"));assert!(!confirmation(action,37,&format!("{action} 36")));}assert!(validate_upload("{}").is_err());assert!(validate_upload(include_str!("../templates/default-1.36.json")).is_ok());}
}
