// SPDX-License-Identifier: GPL-3.0-or-later
//! Note-only replacement compiler. The template supplies validated binary provenance.
use crate::{document,midi_plan,smf::Source};
use serde_json::{json,Value};
use std::collections::BTreeMap;
const STEPS:[u64;17]=[8,12,16,18,24,32,36,48,64,72,96,128,144,192,256,288,384];
fn number(v:&Value,k:&str)->Result<u64,String>{v[k].as_u64().ok_or_else(||format!("Missing integer {k}"))}
// End-before-start ordering matches note management preceding step scheduling.
fn peak_queued(existing:&[(u64,u64,u64,u64)],visit:u64,end:u64,period:u64)->usize{
 let mut edges=vec![];
 for cycle in 0..3{
  let shift=cycle*period;
  edges.push((visit+shift,1i32));edges.push((end+shift,-1));
  for &(a,_,b,_) in existing{edges.push((a+shift,1));edges.push((b+shift,-1));}
 }
 edges.sort_unstable();let(mut count,mut peak)=(0i32,0i32);
 for (_,delta) in edges{count+=delta;peak=peak.max(count);}
 peak as usize
}
pub fn compile(source:&Source,options:&midi_plan::Options,template:&Value,port:u8)->Result<Value,String>{
 if ![0,1,3,4].contains(&port){return Err("Output port must be 0 DIN1, 1 DIN2, 3 USB-host, or 4 USB-device; CV is unsupported".into());}
 let (_,base)=document::import(template)?;
 if base[..4]!=[35,0,1,0]{return Err("MIDI import requires a valid 1.35/schema2 template; no implicit migration".into());}
 let mut doc=template.clone();let mut plan=midi_plan::plan(source,options)?;
 let step=number(&plan["chosen"],"step_ticks")?;
 let index=STEPS.iter().position(|n|*n==step).ok_or("Unsupported grid")?;
 let motion=number(&plan["chosen"],"motion_length")?;
 let start=plan["source_start_carbon_ticks"].as_f64().ok_or("Missing start")?;
 let mut us=500000;for &(tick,tempo) in &source.tempos{if tick as f64*96.0/source.ppq as f64<=start{us=tempo;}}
 let requested_bpm=60_000_000.0/us as f64;let bpm=requested_bpm.clamp(30.0,300.0);
 let mut notes=plan["chosen"]["notes"].as_array().ok_or("Missing notes")?.clone();
 notes.sort_by_key(|n|(n["attack_tick"].as_u64(),n["track"].as_u64(),n["slot"].as_u64()));
 let mut accepted=vec![];let mut extra_losses=vec![];
 let mut channels=BTreeMap::new();
 // Runtime slots are occupied from step visit, including pending delays. Check
 // loop tails too, not just the first pass. Same-pitch overlaps are conservatively
 // removed across shared output channels: MIDI releases lack voice identifiers.
 let loop_ticks=motion*step;
 let mut intervals:BTreeMap<u64,Vec<(u64,u64,u64,u64)>>=BTreeMap::new();
 let mut output_intervals:BTreeMap<(u64,u64),Vec<(u64,u64)>>=BTreeMap::new();
 for note in notes{
  let track=number(&note,"track")?;let ch=number(&note["source"],"source_channel")?;
  let onset=number(&note,"attack_tick")?;let end=number(&note,"release_tick")?;
  let visit=(number(&note,"step")?-1)*step;let pitch=number(&note,"pitch")?;
  let entries=intervals.entry(track).or_default();
  let channel_conflict=channels.get(&track).is_some_and(|c|*c!=ch);
  let overlap=output_intervals.get(&(ch,pitch)).is_some_and(|xs|xs.iter().any(|&(a,b)|
   a<end && onset<b || a+loop_ticks<end && onset<b+loop_ticks || onset+loop_ticks<b && a<end+loop_ticks));
  let occupied=peak_queued(entries,visit,end,loop_ticks);
  if channel_conflict || overlap || occupied>16{
   extra_losses.push(json!({"kind":"dropped","note":note["source"],"reason":if channel_conflict{"source channel conflicts with destination lane"}else if overlap{"same-pitch overlap at shared output channel or loop-tail conflict"}else{"16 active/pending runtime voices"}}));continue;
  }
  channels.insert(track,ch);entries.push((visit,onset,end,pitch));
  output_intervals.entry((ch,pitch)).or_default().push((onset,end));accepted.push(note);
 }
 // Replacement scope is explicit: whole shared event bank, all six scenes.
 for t in 0..6{
  for s in 0..64{
   let step_doc=&mut doc["shared_tracks"][t]["steps"][s];step_doc["delay"]=document::delay_json(0);step_doc["ratchet"]=json!(1);
   for p in 0..6{step_doc["events"][p]=json!({"slot":p+1,"kind":"Empty","parameters":{}});}
  }
 }
 for n in &accepted{
  let t=number(n,"track")? as usize-1;let s=number(n,"step")? as usize-1;let p=number(n,"slot")? as usize-1;
  doc["shared_tracks"][t]["steps"][s]["events"][p]=json!({"slot":p+1,"kind":"Note","parameters":{"pitch":n["pitch"],"velocity":n["velocity"],"length_ticks":n["length_ticks"],"delay":document::delay_json(number(n,"delay_code")? as u8)}});
 }
 let settings=doc["settings"].as_object_mut().ok_or("Missing settings")?;
 for (key,value) in [("tempo",json!(bpm)),("swing",json!(50)),("song_root",json!(-1)),("key_velocity_scale",json!(0)),("metronome",json!(0)),("autolive",json!(0)),("midi_remote",json!(0)),("clock_source",json!(-1))]{settings.insert(key.into(),value);}
 for t in 1..=6{
  let ch=channels.get(&t).copied().unwrap_or(1)-1;
  for (name,val) in [("key_split",0),("type",0),("output1.port",port as i64),("output1.channel_zero_based",ch as i64),("output1.program",-1),("output2.port",-1),("output2.program",-1)]{settings.insert(format!("track{t}.{name}"),json!(val));}
  for scene in 1..=6{
   settings.insert(format!("scene{scene}.root"),json!(-1));
   for (name,val) in [("step_length_index",index as i64),("motion_start_zero_based",0),("motion_length",motion as i64),("scale_index",0),("transpose",0),("bias_source_zero_based",-1),("gate_raw",128),("pattern",31),("reverse",0),("mute",if channels.contains_key(&t){0}else{1}),("arp_enabled",0),("density",100),("rotation",0),("output1.program",-1),("output2.program",-1)]{settings.insert(format!("scene{scene}.track{t}.{name}"),json!(val));}
  }
 }
 for entry in doc["song_list"].as_array_mut().ok_or("Missing song list")?{entry["scene_code"]=json!(-1);}
 let mut expected=vec![];
 for n in &accepted{expected.push((number(n,"track")?,number(n,"step")?,number(n,"slot")?,number(n,"attack_tick")?,number(n,"length_ticks")?,number(n,"pitch")?,number(n,"velocity")?));}
 plan["compiled_timeline"]=json!(accepted);plan["compiler_losses"]=json!(extra_losses);plan["compiled_notes"]=json!(expected.len());
 plan["compile_policy"]=json!({"scope":"replace all six shared tracks and neutralize all scenes; arrangement cleared; output2/programs disabled","output_port":port,"channel_policy":"preserve source channel per lane; drop conflicting channels; same-channel tracks merge at MIDI destination; conflicting same-pitch overlaps dropped across lanes","tempo_bpm":bpm,"requested_initial_bpm":requested_bpm,"tempo_policy":"constant tempo at selection start, clamped 30..300; later tempo changes ignored, original map retained in report","source_tempo_map":source.tempos});
 let mut original_seconds=0.0;let mut cursor=start;let mut tempo=us;
 let finish=plan["source_end_carbon_ticks"].as_f64().ok_or("Missing source end")?;
 for &(tick,next_tempo) in &source.tempos{
  let boundary=tick as f64*96.0/source.ppq as f64;
  if boundary>start && boundary<finish{original_seconds+=(boundary-cursor)/96.0*tempo as f64/1_000_000.0;cursor=boundary;tempo=next_tempo;}
 }
 original_seconds+=(finish-cursor)/96.0*tempo as f64/1_000_000.0;
 let covered=plan["chosen"]["covered_ticks"].as_f64().ok_or("Missing coverage")?;
 let playback_seconds=motion as f64*step as f64/96.0*60.0/bpm;
 plan["compile_policy"]["source_selection_seconds"]=json!(original_seconds);
 plan["compile_policy"]["converted_content_seconds"]=json!(covered/96.0*60.0/bpm);
 plan["compile_policy"]["loop_seconds"]=json!(playback_seconds);
 plan["compile_policy"]["loop_minus_source_seconds"]=json!(playback_seconds-original_seconds);
 doc["metadata"]["extensions"]["midi_import"]=plan;
 let (_,binary)=document::import(&doc)?;
 // Decode event bytes independently of JSON encoding and compare exact timeline.
 let mut actual=vec![];
 for t in 0..6{for s in 0..64{for p in 0..6{
  let o=1824+((t*64+s)*6+p)*6;let e=&binary[o..o+6];if e[0]!=0x90{continue;}
  let delay=match e[3]{0..=96=>e[3] as u64,97..=103=>{let (a,b)=[(1,8),(1,4),(1,3),(1,2),(2,3),(3,4),(7,8)][(e[3]-97) as usize];(step*a+b/2)/b},_=>return Err("Unexpected inherited delay in compilation".into())};
  actual.push((t as u64+1,s as u64+1,p as u64+1,s as u64*step+delay,u16::from_le_bytes([e[4],e[5]]) as u64,e[1] as u64,e[2] as u64));
 }}}
 actual.sort_unstable();expected.sort_unstable();if actual!=expected{return Err("Compiled binary timeline differs from conversion plan".into());}
 Ok(doc)
}

#[cfg(test)]mod tests{
 use super::*;
 fn template()->Value{let mut raw=crate::document::tests::fixture();raw[0]=35;document::export(&raw,30).unwrap()}
 fn source()->Source{Source{ppq:96,format:1,notes:(0..3).map(|i|crate::smf::Note{track:1,channel:2,event:i+1,pitch:60,velocity:90,start:i as u64*32,end:i as u64*32+24}).collect(),tracks:vec![json!({"track":1,"names":["Test"]})],tempos:vec![(0,1000000)],meters:vec![],diagnostics:vec![],end:384,sha256:"test".into()}}
 #[test]fn compilation_timeline_and_neutral_settings(){let t=template();let s=source();let o=midi_plan::Options{start_bar:1,step:Some(96),..Default::default()};let d=compile(&s,&o,&t,4).unwrap();let (_,raw)=document::import(&d).unwrap();assert_eq!(d["metadata"]["extensions"]["midi_import"]["compiled_notes"],3);assert_eq!(d["provenance"],t["provenance"]);assert_eq!(d["settings"]["track1.output1.port"],4);assert_eq!(d["settings"]["track1.output1.channel_zero_based"],1);assert_eq!(d["settings"]["scene6.track1.gate_raw"],128);assert_eq!(raw[1830],0x90);assert_eq!(raw[1833],32);assert_eq!(d["settings"]["tempo"],60.0);}
 #[test]fn rejects_legacy_and_cv(){let mut raw=crate::document::tests::fixture();let t=document::export(&raw,30).unwrap();assert!(compile(&source(),&midi_plan::Options{start_bar:1,..Default::default()},&t,4).is_err());raw[0]=35;assert!(compile(&source(),&midi_plan::Options{start_bar:1,..Default::default()},&template(),2).is_err());}
 #[test]#[ignore="requires local user MIDI; no redistributable fixture"]fn local_bach_acceptance(){let root=std::env::var("CARBON_MIDI_TEST_ROOT").expect("set local MIDI directory");for (file,count) in [("bwv599.mid",317),("bwv-988-aria.mid",418)]{let s=crate::smf::read_file(&std::path::Path::new(&root).join(file)).unwrap();let d=compile(&s,&midi_plan::Options{start_bar:1,..Default::default()},&template(),4).unwrap();let r=&d["metadata"]["extensions"]["midi_import"];println!("{file}: planned {} compiled {} compiler_losses {}",r["chosen"]["retained_notes"],r["compiled_notes"],r["compiler_losses"].as_array().unwrap().len());assert_eq!(s.notes.len(),count);if file=="bwv599.mid"{assert_eq!(r["compiled_notes"],317);assert_eq!(r["chosen"]["total_absolute_onset_release_error_ticks"],0.0);}}}
 #[test]fn queued_voice_sweep(){
  let sequential:Vec<_>=(0..30).map(|i|(i*2,i*2,i*2+1,60)).collect();
  assert_eq!(peak_queued(&sequential,0,64,96),2);
  let chord:Vec<_>=(0..16).map(|p|(0,0,24,p)).collect();
  assert_eq!(peak_queued(&chord,0,24,96),17);
  assert_eq!(peak_queued(&[(90,90,110,60)],0,20,96),2);
 }
 #[test]fn shared_channel_overlap_across_tracks(){
  let mut s=source();s.tracks.push(json!({"track":2,"names":["Other"]}));
  s.notes.push(crate::smf::Note{track:2,channel:2,event:1,pitch:60,velocity:80,start:0,end:24});
  let d=compile(&s,&midi_plan::Options{start_bar:1,step:Some(96),..Default::default()},&template(),4).unwrap();
  let r=&d["metadata"]["extensions"]["midi_import"];assert_eq!(r["compiled_notes"],3);
  assert!(r["compiler_losses"][0]["reason"].as_str().unwrap().contains("shared output channel"));
 }
 #[test]fn overlap_loss_is_visible(){let mut s=source();s.notes[0].end=100;let d=compile(&s,&midi_plan::Options{start_bar:1,step:Some(96),..Default::default()},&template(),0).unwrap();let report=&d["metadata"]["extensions"]["midi_import"];assert_eq!(report["compiled_notes"],1);assert_eq!(report["compiler_losses"].as_array().unwrap().len(),2);}
}
