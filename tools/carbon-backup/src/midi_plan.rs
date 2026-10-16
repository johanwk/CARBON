// SPDX-License-Identifier: GPL-3.0-or-later
//! Deterministic, offline Note-only approximation. No image/device operations.
use crate::smf::Source;
use serde_json::{json,Value};
use std::collections::{BTreeMap,BTreeSet};

#[derive(Debug,Default)]
pub struct Options {pub start_bar:u64,pub bars:Option<u64>,pub tracks:Vec<String>,pub step:Option<u64>}
const STEPS:[u64;17]=[8,12,16,18,24,32,36,48,64,72,96,128,144,192,256,288,384];
impl Options {
 pub fn parse(args:&[String])->Result<Self,String>{
  let mut o=Self{start_bar:1,..Self::default()};let mut seen=BTreeSet::new();
  if args.len()%2!=0{return Err("Options require values".into());}
  for p in args.chunks_exact(2){
   if !seen.insert(p[0].clone()){return Err("Repeated option".into());}
   match p[0].as_str(){
    "--start-bar"=>{o.start_bar=p[1].parse().map_err(|_|"Invalid starting bar")?;if o.start_bar==0{return Err("Bars start at 1".into());}},
    "--bars"=>{let n=p[1].parse().map_err(|_|"Invalid bar count")?;if n==0{return Err("Bar count must be positive".into());}o.bars=Some(n);},
    "--tracks"=>{o.tracks=p[1].split(',').map(str::to_owned).collect();if o.tracks.iter().any(String::is_empty){return Err("Empty track selector".into());}},
    "--step-length"=>{if p[1]!="auto"{let n=p[1].parse().map_err(|_|"Step length is auto or CARBON ticks")?;if !STEPS.contains(&n){return Err("Unsupported fixed step length".into());}o.step=Some(n);}},
    _=>return Err(format!("Unknown option {}",p[0]))
   }
  }Ok(o)
 }
}
fn selected_tracks(s:&Source,o:&Options)->Result<Vec<usize>,String>{
 if o.tracks.is_empty(){return Ok(s.tracks.iter().filter(|t|s.notes.iter().any(|n|Some(n.track as u64)==t["track"].as_u64())).filter_map(|t|t["track"].as_u64().map(|n|n as usize)).collect());}
 let mut result=vec![];
 for name in &o.tracks{
  let matches:Vec<_>=s.tracks.iter().filter(|t|{
   if let Ok(n)=name.parse::<u64>(){t["track"]==n}else{t["names"].as_array().is_some_and(|names|names.iter().any(|v|v.as_str()==Some(name)))}
  }).filter_map(|t|t["track"].as_u64().map(|n|n as usize)).collect();
  if matches.len()!=1{return Err(format!("Track {name:?} is missing or ambiguous"));}
  if result.contains(&matches[0]){return Err("Duplicate track selection".into());}result.push(matches[0]);
 }Ok(result)
}
fn range(s:&Source,o:&Options)->Result<(f64,f64),String>{
 let r=s.report();let total=r["bars"].as_u64().unwrap_or(0);
 if o.start_bar==0 || o.start_bar>total{return Err("Starting bar outside file".into());}
 let stop_bar=if let Some(n)=o.bars{o.start_bar.checked_add(n).ok_or("Bar range overflow")?}else{total+1};
 if stop_bar>total+1{return Err("Requested bars extend beyond file".into());}
 let boundary=|bar:u64|->Result<f64,String>{
  if bar==total+1{return Ok(s.end as f64*96.0/s.ppq as f64);}
  for sec in r["bar_sections"].as_array().into_iter().flatten(){
   let first=sec["first_bar"].as_u64().unwrap_or(0);let count=sec["bars"].as_u64().unwrap_or(0);
   if bar>=first && bar<first+count{
    let start=sec["start_tick"].as_u64().unwrap_or(0) as f64*96.0/s.ppq as f64;
    let width=384.0*sec["numerator"].as_u64().unwrap_or(4) as f64/sec["denominator"].as_u64().unwrap_or(4) as f64;
    return Ok(start+(bar-first) as f64*width);
   }
  }Err("Cannot resolve bar boundary".into())
 };Ok((boundary(o.start_bar)?,boundary(stop_bar)?))
}
fn delay_choices(step:u64)->Vec<(u8,u64)>{
 let mut v:Vec<_>=(0..=96u8).map(|c|(c,c as u64)).filter(|c|c.1<step).collect();
 for (i,(n,d)) in [(1,8),(1,4),(1,3),(1,2),(2,3),(3,4),(7,8)].into_iter().enumerate(){let ticks=(step*n+d/2)/d;if ticks<step{v.push((97+i as u8,ticks));}}
 v
}
struct Candidate {report:Value,dropped:usize,error:f64}
fn candidate(s:&Source,tracks:&[usize],start:f64,end:f64,step:u64)->Candidate{
 let capacity=64*step;let span=end-start;let kept_end=span.min(capacity as f64);
 let choices=delay_choices(step);let mut events=vec![];let mut losses=vec![];
 let mut lanes:BTreeMap<usize,Vec<usize>>=BTreeMap::new();let mut next_lane=1usize;
 // Reserve one lane for each selected source, then distribute spare lanes.
 for &track in tracks.iter().take(6){lanes.insert(track,vec![next_lane]);next_lane+=1;}
 let mut bins=BTreeMap::<(usize,u64),usize>::new();let mut error=0.0;
 for &source_track in tracks{
  for n in s.notes.iter().filter(|n|n.track==source_track){
   let original_start=n.start as f64*96.0/s.ppq as f64;let original_end=n.end as f64*96.0/s.ppq as f64;
   if original_start>=end || original_end<=start{continue;}
   let attack=(original_start-start).max(0.0);let release=(original_end-start).min(span);
   let id=json!({"source_track":n.track,"source_channel":n.channel,"source_event":n.event,"pitch":n.pitch});
   if attack>=kept_end || !lanes.contains_key(&source_track){losses.push(json!({"note":id,"kind":"dropped","reason":if attack>=kept_end{"outside 64-step capacity"}else{"more than six source tracks"}}));continue;}
   // Search containing and adjacent steps. Equal errors favour earlier onset,
   // then absolute delay code. Never place an attack past the selected end.
   let parent=(attack/step as f64).floor() as u64;
   let mut possible=vec![];
   for slot in parent.saturating_sub(1)..=(parent+1).min(63){for &(code,offset) in &choices{
    let onset=slot*step+offset;if (onset as f64)<kept_end{possible.push(((onset as f64-attack).abs(),onset,slot,code));}
   }}
   possible.sort_by(|a,b|a.0.total_cmp(&b.0).then(a.1.cmp(&b.1)).then(a.3.cmp(&b.3)));
   let best=possible[0];let slot=best.2;
   let mut lane=lanes[&source_track].iter().copied().find(|l|bins.get(&(*l,slot)).copied().unwrap_or(0)<6);
   if lane.is_none() && next_lane<=6{lanes.get_mut(&source_track).unwrap().push(next_lane);lane=Some(next_lane);next_lane+=1;}
   let Some(lane)=lane else{losses.push(json!({"note":id,"kind":"dropped","reason":"six slots full across allocated tracks"}));continue;};
   let count=bins.entry((lane,slot)).or_default();*count+=1;
   let requested_duration=(release-attack).max(0.0);
   let duration=requested_duration.round().clamp(1.0,65535.0).min((kept_end-best.1 as f64).floor().max(1.0)) as u64;
   let onset_error=best.1 as f64-attack;let release_error=best.1 as f64+duration as f64-release;
   error+=onset_error.abs()+release_error.abs();
   if onset_error!=0.0 || release_error!=0.0 || original_start<start || original_end>end{losses.push(json!({"note":id,"kind":"timing_adjustment","onset_error_ticks":onset_error,"release_error_ticks":release_error,"clipped_at_start":original_start<start,"clipped_at_end":original_end>end || release>kept_end}));}
   events.push(json!({"source":id,"track":lane,"step":slot+1,"slot":*count,"pitch":n.pitch,"velocity":n.velocity,"length_ticks":duration,"delay_code":best.3,"attack_tick":best.1,"release_tick":best.1+duration}));
  }
 }
 let dropped=losses.iter().filter(|l|l["kind"]=="dropped").count();
 let mapping:Vec<_>=lanes.iter().map(|(source,dest)|json!({"source_track":source,"carbon_tracks":dest})).collect();
 Candidate{dropped,error,report:json!({"step_ticks":step,"motion_length":((kept_end/step as f64).ceil() as u64).clamp(1,64),"capacity_ticks":capacity,"covered_ticks":kept_end,"truncated_ticks":(span-kept_end).max(0.0),"retained_notes":events.len(),"dropped_notes":dropped,"total_absolute_onset_release_error_ticks":error,"mapping":mapping,"notes":events,"losses":losses})}
}
pub fn plan(s:&Source,o:&Options)->Result<Value,String>{
 let tracks=selected_tracks(s,o)?;let (start,end)=range(s,o)?;
 let steps=if let Some(step)=o.step{if !STEPS.contains(&step){return Err("Unsupported step length".into());}vec![step]}else{STEPS.to_vec()};
 let mut candidates:Vec<_>=steps.into_iter().map(|step|candidate(s,&tracks,start,end,step)).collect();
 candidates.sort_by(|a,b|a.dropped.cmp(&b.dropped).then_with(||a.report["truncated_ticks"].as_f64().unwrap_or(0.0).total_cmp(&b.report["truncated_ticks"].as_f64().unwrap_or(0.0))).then_with(||a.error.total_cmp(&b.error)).then_with(||b.report["step_ticks"].as_u64().cmp(&a.report["step_ticks"].as_u64())));
 let alternatives:Vec<_>=candidates.iter().map(|c|json!({"step_ticks":c.report["step_ticks"],"dropped_notes":c.dropped,"truncated_ticks":c.report["truncated_ticks"],"error_ticks":c.error})).collect();
 Ok(json!({"format":"carbon-midi-plan","schema_version":1,"source_sha256":s.sha256,"start_bar":o.start_bar,"requested_bars":o.bars,"selected_source_tracks":tracks,"source_start_carbon_ticks":start,"source_end_carbon_ticks":end,"chosen":candidates.remove(0).report,"alternatives":alternatives,"source_diagnostics":s.diagnostics,"warnings":["Offline Note-only plan, not a song image or upload.","Sustain/controllers/bend/pressure/programs are not rendered. Missing releases use analysis repair policy.","Tempo map is not compiled; note coordinates preserve musical beats, not changing-tempo wall time.","Runtime voice capacity, routing and same-pitch output FIFO are not yet simulated; do not treat retained notes as hardware acceptance."],"policy":"Selected source order; source note onset/event order retained first. Spare tracks split density; later excess notes dropped. Nearest onset across adjacent steps; ties earlier then lower code. Auto ranks dropped notes, truncated span, timing error, longer steps. No time compression."}))
}

#[cfg(test)] mod tests{
 use super::*;
 fn source()->Source{Source{ppq:96,format:1,notes:vec![],tracks:vec![json!({"track":1,"names":["Upper"]}),json!({"track":2,"names":["Lower"]})],tempos:vec![],meters:vec![],diagnostics:vec![],end:768,sha256:"test".into()}}
 fn note(track:usize,event:usize,start:u64,end:u64)->crate::smf::Note{crate::smf::Note{track,channel:track as u8,event,pitch:60,velocity:100,start,end}}
 #[test] fn exact_repeated_pitch_and_named_selection(){let mut s=source();s.notes=vec![note(1,1,0,24),note(1,2,32,56),note(1,3,64,88)];let o=Options{start_bar:1,bars:Some(1),tracks:vec!["Upper".into()],step:Some(96)};let p=plan(&s,&o).unwrap();assert_eq!(p["chosen"]["retained_notes"],3);assert_eq!(p["chosen"]["losses"],json!([]));assert_eq!(p["chosen"]["notes"][2]["slot"],3);}
 #[test] fn selection_edges(){let mut s=source();s.notes=vec![note(1,1,360,420),note(1,2,700,800)];let o=Options{start_bar:2,bars:Some(1),tracks:vec![],step:Some(96)};let p=plan(&s,&o).unwrap();assert_eq!(p["chosen"]["notes"][0]["attack_tick"],0);assert_eq!(p["chosen"]["losses"].as_array().unwrap().len(),2);}
 #[test] fn dense_split_and_drop(){let mut s=source();s.notes=(0..40).map(|i|note(1,i,0,24)).collect();let o=Options{start_bar:1,step:Some(96),..Options::default()};let p=plan(&s,&o).unwrap();assert_eq!(p["chosen"]["retained_notes"],36);assert_eq!(p["chosen"]["dropped_notes"],4);assert_eq!(p,plan(&s,&o).unwrap());}
 #[test] fn capacity_and_quantization(){let mut s=source();s.end=30000;s.notes=vec![note(1,1,150,180),note(1,2,29000,29100)];let o=Options{start_bar:1,step:Some(192),..Options::default()};let p=plan(&s,&o).unwrap();assert_eq!(p["chosen"]["dropped_notes"],1);assert!(p["chosen"]["total_absolute_onset_release_error_ticks"].as_f64().unwrap()>0.0);}
 #[test] fn bad_selectors_and_options(){let s=source();for args in [vec!["--start-bar","0"],vec!["--bars","0"],vec!["--step-length","999"],vec!["--unknown","1"]]{assert!(Options::parse(&args.iter().map(|x|x.to_string()).collect::<Vec<_>>()).is_err());}for tracks in [vec!["Missing".into()],vec!["1".into(),"Upper".into()]]{assert!(plan(&s,&Options{start_bar:1,tracks,..Options::default()}).is_err());}}
}
