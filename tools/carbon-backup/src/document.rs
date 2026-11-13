// SPDX-License-Identifier: GPL-3.0-or-later
//! Canonical JSON v1 prototype: complete image document, not a sparse patch.
use crate::{hash,song,song_range,restore,SONG_SIZE};
use serde_json::{json,Value};
/// Parse strictly and compile in memory using the same checks as import-json.
/// Validity is not firmware detection, target approval or playback simulation.
pub fn validate_text(text:&str)->Result<String,String>{
 let doc=crate::json::parse(text)?;
 let (_,data)=import(&doc)?;
 Ok(format!("Valid CARBON schema{} / firmware profile {}\nBinary compilation validated: {} bytes\nBinary SHA256 {}\nNo files written or device access. Upload still requires a compatible firmware assertion, target backup and exact-plan approval.",
  doc["schema_version"],doc["firmware_profile"].as_str().ok_or("Missing firmware profile")?,data.len(),hash(&data)))
}

const TYPES:[(u8,&str);12]=[(0,"Empty"),(1,"CCi"),(2,"Prb"),(3,"Euc"),(4,"Pre"),(5,"NotPre"),(6,"Hrm"),(7,"Inv"),(8,"Gli"),(0x90,"Note"),(0xb0,"CC"),(0xc0,"PC")];
fn keys(v:&Value,expected:&[&str])->Result<(),String>{let m=v.as_object().ok_or("Expected object")?;if m.len()!=expected.len()||m.keys().any(|k|!expected.contains(&k.as_str())){return Err(format!("Missing/unknown fields; expected{expected:?}"));}Ok(())}
fn num(v:&Value,max:u64)->Result<u64,String>{let n=v.as_u64().ok_or("Unsigned integer required")?;if n>max{return Err("Integer out of range".into());}Ok(n)}
fn integer(v:&Value,min:i64,max:i64)->Result<i64,String>{let n=v.as_i64().ok_or("Integer required")?;if n<min||n>max{return Err("Integer out of range".into());}Ok(n)}
const FRACTIONS:[&str;7]=["1/8","1/4","1/3","1/2","2/3","3/4","7/8"];
pub fn delay_json(code:u8)->Value{match code{255=>json!({"kind":"inherit"}),97..=103=>json!({"kind":"relative","fraction":FRACTIONS[(code-97) as usize]}),_=>json!({"kind":"ticks","ticks":code})}}
fn delay_code(v:&Value,inherit:bool)->Result<u8,String>{match v["kind"].as_str(){
 Some("inherit") if inherit=>{keys(v,&["kind"])?;Ok(255)},
 Some("ticks")=>{keys(v,&["kind","ticks"])?;Ok(num(&v["ticks"],96)? as u8)},
 Some("relative")=>{keys(v,&["kind","fraction"])?;let f=v["fraction"].as_str().ok_or("Missing fraction")?;Ok(97+FRACTIONS.iter().position(|x|*x==f).ok_or("Unsupported delay fraction")? as u8)},
 _=>Err("Invalid delay choice".into())}}
fn parameters(e:&[u8])->Value{match e[0]{
 0|4|5=>json!({}),1|0xb0=>json!({"controller":e[1],"value":e[2]}),2=>json!({"percent":e[1]}),
 3=>json!({"pair_index":e[1],"rotation":e[2]}),6=>json!({"scale_choice":e[1],"root_choice":e[2]}),
 8=>json!({"duration_choice":e[1],"curve":e[2]}),
 7=>json!({"rotations":e[1],"compact":e[2]!=0}),0xc0=>json!({"program":e[1]}),
 _=>json!({"pitch":e[1],"velocity":e[2],"length_ticks":u16::from_le_bytes([e[4],e[5]])})}}
pub fn export(data:&[u8],slot:usize)->Result<Value,String>{
 song_range(slot)?;restore::validate_source(data)?;song::validate(data)?;
 let mut tracks=vec![];
 for t in 0..6{let mut steps=vec![];for s in 0..64{let mut events=vec![];for p in 0..6{let o=1824+((t*64+s)*6+p)*6;let e=&data[o..o+6];
 let mut params=parameters(e);if data[0]>=35 && e[0]==0x90{params["delay"]=delay_json(e[3]);}
 events.push(json!({"slot":p+1,"kind":TYPES.iter().find(|(n,_)|*n==e[0]).unwrap().1,"parameters":params}));}
 let o=1056+2*(t*64+s);let mut step=json!({"step":s+1,"delay_ticks":data[o],"ratchet":data[o+1],"events":events});if data[0]>=35{step.as_object_mut().unwrap().remove("delay_ticks");step["delay"]=delay_json(data[o]);}steps.push(step);}
 tracks.push(json!({"track":t+1,"steps":steps}));}
 let list=(0..64).map(|i|{let o=48+6*i;json!({"entry":i+1,"scene_code":data[o] as i8,"length_beats":i16::from_le_bytes([data[o+2],data[o+3]]),"transpose":data[o+4] as i8})}).collect::<Vec<_>>();
 Ok(json!({"format":"carbon-song","schema_version":if data[0]==36{3}else if data[0]>=35{2}else{1},"firmware_profile":if data[0]==36{"carbon-1.36-glide"}else if data[0]>=35{"carbon-1.35-note-delay"}else{"carbon-1.34-shared-events"},
 "metadata":{"title":"","description":"","extensions":{}},"dependencies":[],
 "provenance":{"capture_slot":slot,"raw_sha256":hash(data),"raw_hex":data.iter().map(|v|format!("{v:02x}")).collect::<String>()},
 "settings":song::export_fields(data)?,"song_list":list,"shared_tracks":tracks}))
}
/// Preserve host-only metadata when re-exporting a compiled archive.
pub fn export_archive(manifest:&Value,data:&[u8],slot:usize)->Result<Value,String>{
 let mut doc=export(data,slot)?;
 if let Some(metadata)=manifest.get("document_metadata"){doc["metadata"]=metadata.clone();}
 // Validate carried metadata with the same rules as imported JSON.
 import(&doc)?;Ok(doc)
}
pub fn import(doc:&Value)->Result<(usize,Vec<u8>),String>{
 keys(doc,&["format","schema_version","firmware_profile","metadata","dependencies","provenance","settings","song_list","shared_tracks"])?;
 let glide=doc["schema_version"]==3 && doc["firmware_profile"]=="carbon-1.36-glide";
 let modern=glide || (doc["schema_version"]==2 && doc["firmware_profile"]=="carbon-1.35-note-delay");
 if doc["format"]!="carbon-song"||(!modern && (doc["schema_version"]!=1||doc["firmware_profile"]!="carbon-1.34-shared-events")){return Err("Unsupported schema/firmware capability profile".into());}
 keys(&doc["metadata"],&["title","description","extensions"])?;
 if !doc["metadata"]["title"].is_string()||!doc["metadata"]["description"].is_string()||!doc["metadata"]["extensions"].is_object(){return Err("Invalid metadata".into());}
 if doc["dependencies"].as_array().ok_or("dependencies must be array")?.len()!=0{return Err("Dependency installation not implemented; preserve separately".into());}
 let prov=&doc["provenance"];keys(prov,&["capture_slot","raw_sha256","raw_hex"])?;
 let slot=num(&prov["capture_slot"],64)? as usize;song_range(slot)?;
 let text=prov["raw_hex"].as_str().ok_or("Missing raw provenance")?;
 if text.len()!=SONG_SIZE*2 || !text.bytes().all(|b|b.is_ascii_hexdigit()){return Err("Invalid raw hex".into());}
 let mut data=(0..text.len()).step_by(2).map(|i|u8::from_str_radix(&text[i..i+2],16).unwrap()).collect::<Vec<_>>();
 if prov["raw_sha256"]!=hash(&data){return Err("Provenance hash mismatch".into());}
 if data[0] != (if glide{36}else if modern{35}else{34}){return Err("Profile must match provenance version; migrate explicitly".into());}
 restore::validate_source(&data)?;song::validate(&data)?;song::apply_fields(&mut data,&doc["settings"])?;
 let list=doc["song_list"].as_array().ok_or("Missing song list")?;if list.len()!=64{return Err("Exactly64 list entries required".into());}
 for(i,e)in list.iter().enumerate(){keys(e,&["entry","scene_code","length_beats","transpose"])?;if num(&e["entry"],64)?!=i as u64+1{return Err("List positions duplicate/out of order".into());}
 let o=48+6*i;data[o]=integer(&e["scene_code"],-1,7)? as i8 as u8;data[o+2..o+4].copy_from_slice(&(integer(&e["length_beats"],-32768,32767)? as i16).to_le_bytes());data[o+4]=integer(&e["transpose"],-128,127)? as i8 as u8;}
 let tracks=doc["shared_tracks"].as_array().ok_or("Missing shared tracks")?;if tracks.len()!=6{return Err("Exactly6 tracks required".into());}
 for(t,tr)in tracks.iter().enumerate(){keys(tr,&["track","steps"])?;if num(&tr["track"],6)?!=t as u64+1{return Err("Track order/identity mismatch".into());}
 let steps=tr["steps"].as_array().ok_or("Missing steps")?;if steps.len()!=64{return Err("Exactly64 steps required".into());}
 for(s,step)in steps.iter().enumerate(){keys(step,if modern{&["step","delay","ratchet","events"]}else{&["step","delay_ticks","ratchet","events"]})?;if num(&step["step"],64)?!=s as u64+1{return Err("Step order mismatch".into());}
 let o=1056+2*(t*64+s);let delay=if modern{delay_code(&step["delay"],false)? as u64}else{num(&step["delay_ticks"],96)?};let ratchet=num(&step["ratchet"],8)?;if ratchet==0{return Err("Ratchet must be1..8".into());}data[o]=delay as u8;data[o+1]=ratchet as u8;
 let events=step["events"].as_array().ok_or("Missing slots")?;if events.len()!=6{return Err("Exactly6 ordered slots required".into());}
 for(p,event)in events.iter().enumerate(){keys(event,&["slot","kind","parameters"])?;if num(&event["slot"],6)?!=p as u64+1{return Err("Slot identity/order mismatch".into());}
 let kind=event["kind"].as_str().ok_or("Missing kind")?;let code=TYPES.iter().find(|(_,name)|*name==kind).ok_or("Unknown event kind")?.0;let params=&event["parameters"];let o=1824+((t*64+s)*6+p)*6;data[o]=code;
 let names:&[&str]=match code{0|4|5=>&[],1|0xb0=>&["controller","value"],2=>&["percent"],3=>&["pair_index","rotation"],6=>&["scale_choice","root_choice"],7=>&["rotations","compact"],8=>&["duration_choice","curve"],0xc0=>&["program"],_=>&["pitch","velocity","length_ticks"]};
 if params.get("delay_ticks").is_some(){return Err("Independent note delays require future firmware; not silently quantized".into());}let mut expected=names.to_vec();if modern && code==0x90{expected.push("delay");data[o+3]=delay_code(&params["delay"],true)?;}keys(params,&expected)?;
 for(n,name)in names.iter().enumerate(){match *name{"length_ticks"=>data[o+4..o+6].copy_from_slice(&(num(&params[*name],65535)? as u16).to_le_bytes()),"compact"=>data[o+2]=params[*name].as_bool().ok_or("Compact must be boolean")? as u8,_=>data[o+1+n]=num(&params[*name],255)? as u8}}
 }}}
 restore::validate_source(&data)?;song::validate(&data)?;Ok((slot,data))
}
#[cfg(test)]pub(crate) mod tests{use super::*;
 pub(crate) fn fixture()->Vec<u8>{let mut d=vec![255;SONG_SIZE];d[..4].copy_from_slice(&[34,0,1,0]);d[SONG_SIZE-4..].copy_from_slice(&[0x47,0x4e,0x4f,0x53]);
 d[4..8].copy_from_slice(&120f32.to_le_bytes());d[8]=50;d[9]=1;d[10..12].fill(0);d[12]=2;d[13]=0;d[14..18].fill(255);d[18..26].fill(0);d[28..48].fill(0);
 for t in 0..6{let o=432+t*8;d[o..o+2].fill(255);d[o+2..o+8].fill(0);}
 for i in 0..36{let o=480+16*i;d[o..o+16].fill(0);d[o+3]=255;d[o+5]=64;d[o+6]=128;d[o+12]=12;}
 for i in 0..384{d[1056+2*i]=0;d[1057+2*i]=1;}for i in 0..2304{d[1824+6*i]=0;}
 d[15648]=0;d[15649]=100;d[15650]=255;d[15654..15662].fill(0);d[15662]=1;d[15663]=0;d[15664]=12;d[15665]=100;d[15738..15774].fill(100);d[15774..15810].fill(0);d[15810]=255;d[15811..15817].fill(254);
 d[1830..1836].copy_from_slice(&[0x90,48,96,255,106,0]);d}
 #[test]fn glide_136_roundtrip(){
  let mut raw=fixture();raw[0]=36;
  raw[1824..1830].copy_from_slice(&[8,4,32,255,0,0]);
  raw[1836..1842].copy_from_slice(&[1,35,127,255,0,0]);
  raw[1056]=99;
  let doc=export(&raw,36).unwrap();
  assert_eq!(doc["schema_version"],3);assert_eq!(doc["firmware_profile"],"carbon-1.36-glide");
  assert_eq!(doc["shared_tracks"][0]["steps"][0]["events"][0]["kind"],"Gli");
  assert_eq!(import(&doc).unwrap().1,raw);
  restore::validate_hardware_source(&raw,"--firmware-1.36-gli3").unwrap();
  assert!(restore::validate_hardware_source(&raw,"--firmware-9c91ab0").is_err());
  let mut wrong=doc.clone();wrong["firmware_profile"]=json!("carbon-1.35-note-delay");wrong["schema_version"]=json!(2);assert!(import(&wrong).is_err());
  for (code,a,b) in [(8,9,64),(8,4,128),(1,34,9),(1,35,128)]{
   let mut bad=raw.clone();bad[1824]=code;bad[1825]=a;bad[1826]=b;assert!(restore::validate_source(&bad).is_err());
  }
  for (code,a,b) in [(8,4,64),(1,34,8),(1,35,127)]{
   let mut old=raw.clone();old[0]=35;old[1824]=code;old[1825]=a;old[1826]=b;assert!(restore::validate_source(&old).is_err());
  }
 }
 #[test]fn bundled_default_135(){
  let text=include_str!("../templates/default-1.35.json");
  let doc=crate::json::parse(text).unwrap();let (_,raw)=import(&doc).unwrap();
  assert_eq!(&raw[..4],&[35,0,1,0]);
  assert!(raw[1824..15648].chunks_exact(6).all(|e|e[0]==0));
  assert_eq!(doc["metadata"]["extensions"]["default_template"]["maximum_supported_firmware"],"1.35");
  for cv in 1..=4{assert_eq!(doc["settings"][format!("cv{cv}.calibration")],0);assert_eq!(doc["settings"][format!("cv{cv}.offset")],0);}
  for t in 1..=6{for out in 1..=2{assert_eq!(doc["settings"][format!("track{t}.output{out}.port")],-1);}}
  assert_eq!(export(&raw,36).unwrap()["shared_tracks"],doc["shared_tracks"]);
 }
 #[test]fn validation_only(){
  for version in [34,35]{let mut raw=fixture();raw[0]=version;let doc=export(&raw,19).unwrap();let text=serde_json::to_string(&doc).unwrap();let summary=validate_text(&text).unwrap();assert!(summary.contains("20480 bytes"));assert!(summary.contains(&hash(&raw)));assert!(summary.contains(if version==35{"schema2"}else{"schema1"}));
   let mut bad=doc.clone();bad["provenance"]["raw_sha256"]=json!("wrong");assert!(validate_text(&bad.to_string()).is_err());
   bad=doc;bad["shared_tracks"][0]["steps"][0]["events"][1]["parameters"]["pitch"]=json!(128);assert!(validate_text(&bad.to_string()).is_err());
  }
  assert!(validate_text("{").is_err());assert!(validate_text(r#"{"format":1,"format":2}"#).is_err());assert!(validate_text("{}").is_err());
 }
 #[test]fn lossless_and_patch(){let data=fixture();let mut doc=export(&data,19).unwrap();assert_eq!(import(&doc).unwrap().1,data);doc["shared_tracks"][0]["steps"][0]["events"][1]["parameters"]["velocity"]=json!(97);let(_,after)=import(&doc).unwrap();let changes:Vec<_>=data.iter().zip(&after).enumerate().filter_map(|(i,(a,b))|(a!=b).then_some(i)).collect();assert_eq!(changes,vec![1832]);}
 #[test]fn delay_choices(){
 for code in (0u8..=103).chain(std::iter::once(255)){assert_eq!(delay_code(&delay_json(code),true).unwrap(),code);}
 for bad in [json!({"kind":"ticks","ticks":97}),json!({"kind":"relative","fraction":"1/5"}),json!({"kind":"inherit","extra":0})]{assert!(delay_code(&bad,true).is_err());}
 assert!(delay_code(&delay_json(255),false).is_err());
 }
 #[test]fn note_delay_profile(){
 let mut raw=fixture();raw[0]=35;raw[1833]=99;raw[1056]=101;
 let mut doc=export(&raw,19).unwrap();assert_eq!(doc["schema_version"],2);
 assert_eq!(doc["shared_tracks"][0]["steps"][0]["delay"],json!({"kind":"relative","fraction":"2/3"}));
 assert_eq!(import(&doc).unwrap().1,raw);
 let note=&mut doc["shared_tracks"][0]["steps"][0]["events"][1]["parameters"];
 assert_eq!(note["delay"],json!({"kind":"relative","fraction":"1/3"}));
 note["delay"]=json!({"kind":"inherit"});assert_eq!(import(&doc).unwrap().1[1833],255);
 doc["shared_tracks"][0]["steps"][0]["delay"]=json!({"kind":"inherit"});assert!(import(&doc).is_err());
 raw[1833]=104;assert!(export(&raw,19).is_err());
 }
 #[test]fn settings_and_metadata(){let data=fixture();let mut doc=export(&data,19).unwrap();assert!(doc["settings"].get("scene1.track1.step_length_index").is_some());doc["settings"]["tempo"]=json!(121);doc["metadata"]["title"]=json!("test");let(_,after)=import(&doc).unwrap();assert_eq!(f32::from_le_bytes(after[4..8].try_into().unwrap()),121.);}
 #[test]fn reject_ambiguous(){let doc=export(&fixture(),19).unwrap();let mut d=doc.clone();d["extra"]=json!(1);assert!(import(&d).is_err());let mut d=doc.clone();d["shared_tracks"][0]["steps"][0]["events"][1]["slot"]=json!(1);assert!(import(&d).is_err());let mut d=doc.clone();d["shared_tracks"][0]["steps"][0]["events"][1]["parameters"]["delay_ticks"]=json!(1);assert!(import(&d).unwrap_err().contains("future firmware"));let mut d=doc;d["provenance"]["raw_sha256"]=json!("bad");assert!(import(&d).is_err());}

 #[test]fn metadata_reexport(){let data=fixture();let mut doc=export(&data,19).unwrap();doc["metadata"]["title"]=json!("Test title");doc["metadata"]["extensions"]=json!({"musical_notes":["wide chord"],"custom":{"x":1}});let(_,raw)=import(&doc).unwrap();let manifest=json!({"document_metadata":doc["metadata"]});let again=export_archive(&manifest,&raw,19).unwrap();assert_eq!(again["metadata"],doc["metadata"]);assert_eq!(import(&again).unwrap().1,data);}
}
