// SPDX-License-Identifier: GPL-3.0-or-later
use midly::{Smf,Format,Timing,TrackEventKind,MidiMessage,MetaMessage};
use serde_json::{json,Value};
use std::collections::{BTreeMap,BTreeSet,VecDeque};
use std::io::Read;
pub const MAX_BYTES:usize=8*1024*1024;
#[derive(Debug)]
pub struct Note {pub track:usize,pub channel:u8,pub event:usize,pub pitch:u8,pub velocity:u8,pub start:u64,pub end:u64}
pub struct Source {pub ppq:u16,pub format:u8,pub notes:Vec<Note>,pub tracks:Vec<Value>,pub tempos:Vec<(u64,u32)>,pub meters:Vec<(u64,u32,u32)>,pub diagnostics:Vec<Value>,pub end:u64,pub sha256:String}
pub fn read_file(path:&std::path::Path)->Result<Source,String>{
 let mut bytes=vec![];
 std::fs::File::open(path).map_err(|e|e.to_string())?.take((MAX_BYTES+1) as u64).read_to_end(&mut bytes).map_err(|e|e.to_string())?;
 parse(&bytes)
}
pub fn parse(bytes:&[u8])->Result<Source,String>{
 if bytes.len()>MAX_BYTES{return Err("MIDI exceeds 8 MiB limit".into());}
 if bytes.len()<14 || &bytes[..4]!=b"MThd" || bytes[4..8]!=[0,0,0,6]{return Err("Expected SMF six-byte header".into());}
 // Reject unsupported header timing before midly decodes it: malformed
 // SMPTE -128 can overflow inside midly 0.5.3's debug decoder.
 if bytes[12]&0x80!=0{return Err("SMPTE timing unsupported".into());}
 if bytes[12..14]==[0,0]{return Err("Zero PPQ".into());}
 if u16::from_be_bytes([bytes[8],bytes[9]])>1{return Err("SMF format 2/unknown format unsupported".into());}
 let count=u16::from_be_bytes([bytes[10],bytes[11]]) as usize;
 if count==0 || count>1024{return Err("Track count must be 1..1024".into());}
 let mut pos=14usize;
 for _ in 0..count {
  if bytes.get(pos..pos+4)!=Some(b"MTrk"){return Err("Missing track chunk".into());}
  let len=bytes.get(pos+4..pos+8).ok_or("Truncated track header")?;
  let len=u32::from_be_bytes(len.try_into().map_err(|_|"Track length")?) as usize;
  pos=pos.checked_add(8).and_then(|p|p.checked_add(len)).ok_or("Track overflow")?;
  if pos>bytes.len(){return Err("Truncated track".into());}
 }
 if pos!=bytes.len(){return Err("Trailing MIDI chunks/data unsupported".into());}
 let smf=Smf::parse(bytes).map_err(|e|format!("Invalid MIDI: {e}"))?;
 let format=match smf.header.format{Format::SingleTrack=>0,Format::Parallel=>1,_=>return Err("SMF format 2 unsupported".into())};
 if format==0 && count!=1{return Err("Format 0 requires one track".into());}
 let ppq=match smf.header.timing{Timing::Metrical(n) if n.as_int()>0=>n.as_int(),_=>return Err("Require positive PPQ; SMPTE unsupported".into())};
 if smf.tracks.iter().map(Vec::len).sum::<usize>()>500_000{return Err("MIDI exceeds 500000-event limit".into());}
 let mut s=Source{ppq,format,notes:vec![],tracks:vec![],tempos:vec![],meters:vec![],diagnostics:vec![],end:0,sha256:crate::hash(bytes)};
 let mut tempos=vec![];let mut meters=vec![];
 for (ti,events) in smf.tracks.iter().enumerate(){
  let mut tick=0u64;let mut ended=false;let mut names=vec![];let mut channels=BTreeSet::new();let mut counts=BTreeMap::<String,usize>::new();let mut programs=vec![];
  let mut active=BTreeMap::<(u8,u8),VecDeque<(u64,u8,usize)>>::new();
  for (ei,e) in events.iter().enumerate(){
   if ended{return Err(format!("Track {}: events after EndOfTrack",ti+1));}
   tick=tick.checked_add(e.delta.as_int() as u64).ok_or("Time overflow")?;
   match e.kind {
    TrackEventKind::Midi{channel,message}=>{
     let ch=channel.as_int()+1;channels.insert(ch);
     let kind=match message{MidiMessage::NoteOn{..}=>"note_on",MidiMessage::NoteOff{..}=>"note_off",MidiMessage::Controller{..}=>"controller",MidiMessage::ProgramChange{..}=>"program_change",MidiMessage::PitchBend{..}=>"pitch_bend",MidiMessage::Aftertouch{..}=>"poly_pressure",MidiMessage::ChannelAftertouch{..}=>"channel_pressure"};
     *counts.entry(kind.into()).or_default()+=1;
     match message{
      MidiMessage::NoteOn{key,vel} if vel.as_int()>0=>{active.entry((ch,key.as_int())).or_default().push_back((tick,vel.as_int(),ei+1));},
      MidiMessage::NoteOff{key,..}|MidiMessage::NoteOn{key,..}=>{
       if let Some((start,velocity,event))=active.entry((ch,key.as_int())).or_default().pop_front(){
        s.notes.push(Note{track:ti+1,channel:ch,event,pitch:key.as_int(),velocity,start,end:tick});
        if start==tick{s.diagnostics.push(json!({"kind":"zero_length_note","track":ti+1,"event":event}));}
       }else{s.diagnostics.push(json!({"kind":"unmatched_note_off","track":ti+1,"event":ei+1,"tick":tick}));}
      },
      MidiMessage::ProgramChange{program}=>programs.push(json!({"tick":tick,"channel":ch,"program_raw":program.as_int()})),_=>{}
     }
    },
    TrackEventKind::Meta(m)=>{
     *counts.entry("meta".into()).or_default()+=1;
     match m{
      MetaMessage::TrackName(n)=>names.push(String::from_utf8_lossy(n).into_owned()),
      MetaMessage::Tempo(us)=>{if us.as_int()==0{return Err("Zero tempo".into());}tempos.push((tick,ti,ei,us.as_int()));},
      MetaMessage::TimeSignature(n,d,_,_)=>{if n==0 || d>7{return Err("Invalid/unsupported meter".into());}meters.push((tick,ti,ei,n as u32,1u32<<d));},
      MetaMessage::EndOfTrack=>ended=true,_=>{}
     }
    },_=>{*counts.entry("sysex_or_escape".into()).or_default()+=1;}
   }
  }
  if !ended{return Err(format!("Track {} lacks EndOfTrack",ti+1));}
  for ((channel,pitch),q) in active{for (start,velocity,event) in q{
   let end=tick.max(start+1);s.notes.push(Note{track:ti+1,channel,event,pitch,velocity,start,end});
   s.diagnostics.push(json!({"kind":"missing_note_off","track":ti+1,"event":event,"assumed_end_tick":end}));
  }}
  s.end=s.end.max(tick);
  s.tracks.push(json!({"track":ti+1,"names":names,"channels":channels,"events":counts,"program_changes":programs,"end_tick":tick}));
 }
 s.notes.sort_by_key(|n|(n.start,n.track,n.event));tempos.sort_unstable();meters.sort_unstable();
 for c in tempos{if s.tempos.last().is_some_and(|x|x.0==c.0){s.diagnostics.push(json!({"kind":"simultaneous_tempo","tick":c.0}));}s.tempos.push((c.0,c.3));}
 for c in meters{if s.meters.last().is_some_and(|x|x.0==c.0){s.diagnostics.push(json!({"kind":"simultaneous_meter","tick":c.0}));}s.meters.push((c.0,c.3,c.4));}
 s.end=s.end.max(s.notes.iter().map(|n|n.end).max().unwrap_or(0));Ok(s)
}

impl Source {
 fn seconds_at(&self,end:u64)->f64{
  let (mut seconds,mut prev,mut tempo)=(0.0,0,500_000);
  for &(tick,us) in self.tempos.iter().take_while(|c|c.0<=end){seconds+=(tick-prev) as f64*tempo as f64/self.ppq as f64/1_000_000.0;prev=tick;tempo=us;}
  seconds+(end-prev) as f64*tempo as f64/self.ppq as f64/1_000_000.0
 }
 fn bars(&self)->(u64,Vec<Value>){
  // Units of 1/128 source tick retain fractional meter boundaries exactly.
  let end=self.end*128;let mut changes=BTreeMap::from([(0,(4u32,4u32))]);
  for &(tick,n,d) in &self.meters{changes.insert(tick*128,(n,d));}
  let changes:Vec<_>=changes.into_iter().collect();let mut bars=0;let mut sections=vec![];
  for (i,&(start,(n,d))) in changes.iter().enumerate(){
   if start>=end{break;}
   let stop=changes.get(i+1).map(|c|c.0).unwrap_or(end).min(end);
   let width=self.ppq as u64*4*128*n as u64/d as u64;let len=stop-start;let count=len.div_ceil(width);
   sections.push(json!({"start_tick":start/128,"end_tick":stop/128,"first_bar":bars+1,"bars":count,"numerator":n,"denominator":d,"partial_last_bar":len%width!=0,"mid_bar_meter_change":stop<end && len%width!=0}));bars+=count;
  }(bars,sections)
 }
 pub fn report(&self)->Value{
  let (bars,sections)=self.bars();let mut tracks=self.tracks.clone();
  for t in &mut tracks{
   let number=t["track"].as_u64().unwrap_or(0) as usize;
   let ns:Vec<_>=self.notes.iter().filter(|n|n.track==number).collect();
   let mut edges=vec![];for n in &ns{if n.end>n.start{edges.push((n.start,1i32));edges.push((n.end,-1));}}
   edges.sort_unstable();let (mut active,mut peak)=(0,0);for (_,delta) in edges{active+=delta;peak=peak.max(active);}
   t["notes"]=json!(ns.len());t["pitch_min"]=json!(ns.iter().map(|n|n.pitch).min());t["pitch_max"]=json!(ns.iter().map(|n|n.pitch).max());t["peak_polyphony"]=json!(peak);
  }
  let grids:Vec<_>=[8u64,12,16,18,24,32,36,48,64,72,96,128,144,192,256,288,384].into_iter().map(|step|{
   let mut choices:BTreeSet<u64>=(0..=96).collect();
   for (n,d) in [(1,8),(1,4),(1,3),(1,2),(2,3),(3,4),(7,8)]{choices.insert((step*n+d/2)/d);}
   choices.retain(|c|*c<step);
   let mut bins=BTreeMap::new();let (mut bad,mut max_error,mut sum_error,mut beyond)=(0,0,0u128,0);
   for n in &self.notes{
    let onset=n.start*96;let interval=step*self.ppq as u64;let slot=onset/interval;let offset=onset%interval;
    let err=choices.iter().map(|c|offset.abs_diff(c*self.ppq as u64)).min().unwrap_or(0);
    if err>0{bad+=1;}max_error=max_error.max(err);sum_error+=err as u128;
    *bins.entry((n.track,slot)).or_insert(0usize)+=1;if slot>=64{beyond+=1;}
   }
   json!({"step_ticks":step,"steps_for_span":(self.end*96).div_ceil(step*self.ppq as u64),"max_notes_per_step_per_source_track":bins.values().max().copied().unwrap_or(0),"overflow_steps":bins.values().filter(|n|**n>6).count(),"notes_after_step64":beyond,"inexact_onsets":bad,"max_error_ticks":max_error as f64/self.ppq as f64,"sum_error_ticks":sum_error as f64/self.ppq as f64})
  }).collect();
  let recommended=grids.iter().filter(|g|g["steps_for_span"].as_u64().unwrap_or(u64::MAX)<=64).min_by(|a,b|{
   let key=|g:&Value|(g["overflow_steps"].as_u64().unwrap_or(0),g["inexact_onsets"].as_u64().unwrap_or(0));
   key(a).cmp(&key(b)).then_with(||a["sum_error_ticks"].as_f64().unwrap_or(0.0).total_cmp(&b["sum_error_ticks"].as_f64().unwrap_or(0.0))).then_with(||b["step_ticks"].as_u64().cmp(&a["step_ticks"].as_u64()))
  }).map(|g|g["step_ticks"].clone());
  json!({"format":"carbon-midi-analysis","schema_version":1,"source_sha256":self.sha256,"smf_format":self.format,"ppq":self.ppq,"end_tick":self.end,"quarter_beats":self.end as f64/self.ppq as f64,"seconds":self.seconds_at(self.end),"bars":bars,"bar_sections":sections,"notes":self.notes.len(),"musical_tracks":tracks.iter().filter(|t|t["notes"]!=0).count(),"tracks":tracks,"tempo_changes":self.tempos.iter().map(|(t,u)|json!({"tick":t,"microseconds_per_quarter":u})).collect::<Vec<_>>(),"meter_changes":self.meters.iter().map(|(t,n,d)|json!({"tick":t,"numerator":n,"denominator":d})).collect::<Vec<_>>(),"nonintegral_onsets":self.notes.iter().filter(|n|(n.start*96)%self.ppq as u64!=0).count(),"nonintegral_durations":self.notes.iter().filter(|n|((n.end-n.start)*96)%self.ppq as u64!=0).count(),"diagnostics":self.diagnostics,"grid_estimates":grids,"suggested_step_ticks":recommended,"policies":{
   "bars":"one-based; tick 0 starts bar 1; mid-bar meter change starts a new bar; no pickup inference",
   "notes":"FIFO per source track/channel/pitch; missing releases assumed at track end (minimum one source tick)",
   "expression":"sustain/controllers/bend/pressure counted, not applied to note lengths/pitches; musical voices not inferred",
   "grid":"fixed lengths, containing-step estimates, one CARBON track per source track; no allocation/thinning or runtime voice simulation",
   "suggestion":"full-span candidates ranked by overflowing bins, inexact onsets, total onset error; ties favour longer steps; not a conversion plan",
   "defaults":"120 BPM and 4/4 until overridden; simultaneous metadata last in track/event order wins"}})
 }
}
pub fn render(r:&Value)->String{
 use std::fmt::Write;
 let mut out=String::new();
 writeln!(out,"MIDI analysis (offline; no conversion or device access)").unwrap();
 writeln!(out,"SMF {} / {} PPQ; {} notes in {} musical tracks",r["smf_format"],r["ppq"],r["notes"],r["musical_tracks"]).unwrap();
 writeln!(out,"{} bars; {} quarter-note beats; {:.3} seconds",r["bars"],r["quarter_beats"],r["seconds"].as_f64().unwrap_or(0.0)).unwrap();
 for s in r["bar_sections"].as_array().into_iter().flatten(){writeln!(out,"  Bar {}: {}/{} for {} bars{}",s["first_bar"],s["numerator"],s["denominator"],s["bars"],if s["partial_last_bar"]==true{" (last bar partial)"}else{""}).unwrap();}
 for t in r["tracks"].as_array().into_iter().flatten(){writeln!(out,"Track {} {} channels {}: {} notes, pitches {}..{}, peak polyphony {}\n  Events: {}\n  Programs (raw 0-based): {}",t["track"],t["names"],t["channels"],t["notes"],t["pitch_min"],t["pitch_max"],t["peak_polyphony"],t["events"],t["program_changes"]).unwrap();}
 writeln!(out,"Tempo map: {}",r["tempo_changes"]).unwrap();
 writeln!(out,"Nonintegral CARBON ticks: {} onsets, {} durations",r["nonintegral_onsets"],r["nonintegral_durations"]).unwrap();
 writeln!(out,"Grid estimates: ticks/step | steps | max notes/step/part | overflow bins | inexact onsets | max error ticks").unwrap();
 for g in r["grid_estimates"].as_array().into_iter().flatten(){writeln!(out,"  {} | {} | {} | {} | {} | {:.3}",g["step_ticks"],g["steps_for_span"],g["max_notes_per_step_per_source_track"],g["overflow_steps"],g["inexact_onsets"],g["max_error_ticks"].as_f64().unwrap_or(0.0)).unwrap();}
 writeln!(out,"Suggested full-span step: {} ticks (estimate only)\nDiagnostics: {}",r["suggested_step_ticks"],r["diagnostics"]).unwrap();
 for (k,v) in r["policies"].as_object().into_iter().flatten(){writeln!(out,"{k}: {}",v.as_str().unwrap_or("")).unwrap();}out
}

#[cfg(test)]
mod tests {
 use super::*;
 fn file(format:u16,ppq:u16,tracks:&[&[u8]])->Vec<u8>{
  let mut b=b"MThd\0\0\0\x06".to_vec();b.extend(format.to_be_bytes());b.extend((tracks.len() as u16).to_be_bytes());b.extend(ppq.to_be_bytes());
  for t in tracks{b.extend(b"MTrk");b.extend((t.len() as u32).to_be_bytes());b.extend(*t);}b
 }
 #[test] fn running_status_fifo_and_defaults(){
  let data=file(0,96,&[&[0,0x90,60,100,12,60,90,12,60,0,12,0x80,60,64,0,0xff,0x2f,0]]);
  let s=parse(&data).unwrap();assert_eq!(s.notes.len(),2);assert_eq!((s.notes[0].start,s.notes[0].end),(0,24));assert_eq!((s.notes[1].start,s.notes[1].end),(12,36));
  assert_eq!(s.notes[1].velocity,90);assert_eq!(s.notes[0].channel,1);
  let r=s.report();assert_eq!(r["bars"],1);assert_eq!(r["seconds"],0.1875);assert_eq!(r["tracks"][0]["peak_polyphony"],2);assert!(s.diagnostics.is_empty());
 }
 #[test] fn maps_and_named_tracks(){
  let control=[0,0xff,0x51,3,0x0f,0x42,0x40,0,0xff,0x58,4,3,2,24,8,0x82,0x20,0xff,0x51,3,0x07,0xa1,0x20,0,0xff,0x2f,0];
  let notes=[0,0xff,3,4,b'B',b'a',b's',b's',0,0x91,48,80,0x84,0x40,0x81,48,0,0,0xff,0x2f,0];
  let s=parse(&file(1,96,&[&control,&notes])).unwrap();let r=s.report();
  assert_eq!(r["notes"],1);assert_eq!(r["bars"],2);assert_eq!(r["seconds"],4.5);assert_eq!(r["tracks"][1]["names"][0],"Bass");assert_eq!(r["tracks"][1]["channels"][0],2);
 }
 #[test] fn repair_and_expression_reporting(){
  let s=parse(&file(0,384,&[&[0,0xb0,64,127,0,0x80,60,0,0,0x90,62,90,1,0xff,0x2f,0]])).unwrap();
  assert_eq!(s.notes[0].end,1);assert_eq!(s.diagnostics.len(),2);
  let r=s.report();assert_eq!(r["tracks"][0]["events"]["controller"],1);assert_eq!(r["nonintegral_durations"],1);
 }
 #[test] fn grid_exact_and_density(){
  let mut track=vec![];for key in 60..67{track.extend([0,0x90,key,100]);}
  for key in 60..67{track.extend([if key==60{96}else{0},0x80,key,0]);}track.extend([0,0xff,0x2f,0]);
  let s=parse(&file(0,96,&[&track])).unwrap();let r=s.report();let g=r["grid_estimates"].as_array().unwrap().iter().find(|g|g["step_ticks"]==96).unwrap();
  assert_eq!(g["inexact_onsets"],0);assert_eq!(g["overflow_steps"],1);assert_eq!(g["max_notes_per_step_per_source_track"],7);
 }
 #[test] fn meter_partial_and_empty(){
  let data=file(0,96,&[&[96,0xff,0x58,4,3,2,24,8,96,0xff,0x2f,0]]);
  let r=parse(&data).unwrap().report();assert_eq!(r["bars"],2);assert_eq!(r["bar_sections"][0]["mid_bar_meter_change"],true);
  let r=parse(&file(0,96,&[&[0,0xff,0x2f,0]])).unwrap().report();assert_eq!(r["notes"],0);assert_eq!(r["bars"],0);
 }
 #[test] fn malformed_smoke_no_panics(){
  let base=file(0,96,&[&[0,0x90,60,100,96,0x80,60,0,0,0xff,0x2f,0]]);
  for i in 0..base.len(){for byte in [0,127,128,255]{let mut b=base.clone();b[i]=byte;let _=parse(&b);}}
  for ppq in [1,24,96,384,32767]{let s=parse(&file(0,ppq,&[&[1,0x90,60,100,1,0x80,60,0,0,0xff,0x2f,0]])).unwrap();let _=s.report();}
 }
 #[test] fn rejects_invalid_and_unsupported(){
  let valid=file(0,96,&[&[0,0xff,0x2f,0]]);
  for i in 0..valid.len(){assert!(parse(&valid[..i]).is_err());}
  let mut b=valid.clone();b.push(0);assert!(parse(&b).is_err());
  assert!(parse(&file(2,96,&[&[0,0xff,0x2f,0]])).is_err());
  assert!(parse(&file(0,0xe728,&[&[0,0xff,0x2f,0]])).is_err());
  assert!(parse(&file(0,0,&[&[0,0xff,0x2f,0]])).is_err());
  assert!(parse(&file(0,96,&[&[0,0x90,60,100]])).is_err());
  assert!(parse(&file(0,96,&[&[0,0xff,0x51,3,0,0,0,0,0xff,0x2f,0]])).is_err());
  assert!(parse(&vec![0;MAX_BYTES+1]).is_err());
 }
}
