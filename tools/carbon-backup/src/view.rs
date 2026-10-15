// SPDX-License-Identifier: GPL-3.0-or-later
//! Offline terminal rendering; selections independent of CLI/file loading.
use crate::{document,SONG_SIZE};
use std::fmt::Write;
#[derive(Debug,PartialEq)] pub struct Options {pub track:Option<usize>,pub first_step:usize,pub last_step:usize,pub grid:bool,pub scene:usize,pub color:String,pub ascii:bool}
impl Default for Options {fn default()->Self{Self{track:None,first_step:1,last_step:64,grid:false,scene:1,color:"auto".into(),ascii:false}}}
impl Options {
 pub fn parse(args:&[String])->Result<Self,String>{
  let mut opts=Self::default();let mut i=0;let mut seen=std::collections::HashSet::new();
  while i<args.len(){let flag=&args[i];if !seen.insert(flag.clone()){return Err(format!("Repeated option {flag}"));}
   if flag=="--ascii" {opts.ascii=true;i+=1;continue;}
   let value=args.get(i+1).ok_or_else(||format!("Missing value for {flag}"))?;
   match flag.as_str(){
    "--layout"=>{opts.grid=match value.as_str(){"grid"=>true,"detail"=>false,_=>return Err("Layout must be grid or detail".into())};},
    "--scene"=>{opts.scene=value.parse::<usize>().map_err(|_|"Scene must be1..6")?;if !(1..=6).contains(&opts.scene){return Err("Scene must be1..6".into());}},
    "--color"=>{if !["auto","always","never"].contains(&value.as_str()){return Err("Color must be auto,always,never".into());}opts.color=value.clone();},
    "--track"=>{let n=value.parse::<usize>().map_err(|_|"Track must be1..6")?;if !(1..=6).contains(&n){return Err("Track must be1..6".into());}opts.track=Some(n);},
    "--steps"=>{let parts:Vec<_>=value.split('-').collect();if parts.len()>2{return Err("Use --steps N or A-B".into());}
     let a=parts[0].parse::<usize>().map_err(|_|"Invalid first step")?;let b=if parts.len()==2{parts[1].parse::<usize>().map_err(|_|"Invalid last step")?}else{a};
     if a<1||b>64||a>b{return Err("Step range must be ordered within1..64".into());}opts.first_step=a;opts.last_step=b;},
    _=>return Err(format!("Unknown view option {flag}"))
   }i+=2;
  }Ok(opts)
 }
}
fn pitch(n:u8)->String{let names=["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"];format!("{}{} ({n})",names[(n%12) as usize],i16::from(n)/12-1)}
fn detail(e:&[u8])->(&'static str,String){let a=e[1];let b=e[2];let len=u16::from_le_bytes([e[4],e[5]]);match e[0]{
 0=>("Empty",String::new()),0x90=>("Note",format!("{:<11} velocity {b:3}  length {len} ticks",pitch(a))),
 0xb0=>("CC",format!("controller {a}  value {b}")),0xc0=>("PC",format!("program {a}")),
 1=>("CCi",format!("controller {a}  raw value {b}")),2=>("Prb",format!("{a}%")),
 3=>("Euc",format!("pair index {a}  rotation {b}")),4=>("Pre",String::new()),5=>("!Pre",String::new()),
 6=>("Hrm",format!("scale choice {a}  root choice {b}")),7=>("Inv",format!("rotations {a}  Compact {}",if b==0{"Off"}else{"On"})),
 _=>("Unknown",format!("raw type {} data {a}/{b} length {len}",e[0]))}}
pub fn render(data:&[u8],opts:&Options)->Result<String,String>{
 if data.len()!=SONG_SIZE{return Err("Wrong song size".into());}
 let version=u32::from_le_bytes(data[..4].try_into().unwrap());
 if version!=0x10022{return Err("View currently requires1.34 image; no implicit legacy migration".into());}
 crate::restore::validate_source(data)?;crate::song::validate(data)?;
 if opts.first_step<1||opts.last_step>64||opts.first_step>opts.last_step||opts.track.is_some_and(|t|!(1..=6).contains(&t)){return Err("Invalid view selection".into());}
 if opts.grid{return render_grid(data,opts);}
 let mut out=String::from("CARBON 1.34 — shared events across scenes\nMIDI 60=C4; lengths/delays in96-PPQ ticks. All six slots retained.\nScene motion, pattern and routing settings are not applied in this event view.\n\n");
 for t in 1..=6{if opts.track.is_some_and(|selected|selected!=t){continue;}
  for s in opts.first_step..=opts.last_step{let o=1056+2*((t-1)*64+s-1);
   writeln!(out,"Track {t} / Step {s}  [shared delay {} ticks; ratchet {}]",data[o],data[o+1]).unwrap();
   for p in 1..=6{let o=1824+(((t-1)*64+s-1)*6+p-1)*6;let(kind,text)=detail(&data[o..o+6]);writeln!(out,"  {p}  {kind:<7} {text}").unwrap();}out.push('\n');
  }
 }Ok(out)
}
#[derive(Debug,PartialEq)] struct Cell{step:usize,selected:bool,in_motion:bool,events:usize}
fn cells(data:&[u8],track:usize,opts:&Options)->Vec<Cell>{
 let o=480+16*((opts.scene-1)*6+track-1);let start=data[o+4] as usize;let length=data[o+5] as usize;
 (0..64).map(|s|{let o=1824+((track-1)*64+s)*36;let events=(0..6).filter(|p|data[o+p*6]!=0).count();Cell{step:s+1,selected:(opts.first_step..=opts.last_step).contains(&(s+1)),in_motion:(s+64-start)%64<length,events}}).collect()
}
fn euclidean_label(length:usize,density:usize,rotation:usize)->String{
 if density==100{return format!("Pattern (Euclidean off; raw density {density}%, raw rotation {rotation})");}
 let hits=(density*length+50)/100;
 format!("{}:{}:{} (length:hits:rotation; raw density {density}%, raw rotation {rotation})",length,hits,rotation%length)
}
fn render_grid(data:&[u8],opts:&Options)->Result<String,String>{
 use std::io::IsTerminal;
 if !(1..=6).contains(&opts.scene){return Err("Scene must be1..6".into());}
 let color=opts.color=="always" || (opts.color=="auto" && std::io::stdout().is_terminal() && std::env::var_os("NO_COLOR").is_none() && std::env::var("TERM").ok().as_deref()!=Some("dumb"));
 let mut out=String::from("CARBON — 8x8 stored-event grid (NOT a hardware playback-mask simulation)\nRow-major: steps 1..8, 9..16, ... 57..64. Events shared across scenes.\nLegend: #/■ events inside motion; ./· empty inside; +/▧ events outside; -/░ empty outside.\n        blank = excluded by --steps. Cell number is step number.\nPattern/Euclidean enable masks, gates, mute and live overrides NOT applied.\nGlobal editable preset masks are not in this archive; no ROM-default assumption.\n\n");
 for t in 1..=6{if opts.track.is_some_and(|x|x!=t){continue;}let i=(opts.scene-1)*6+t-1;let o=480+16*i;
 writeln!(out,"Track {t} / Scene {} — motion start {} length {}; preset index {}; Euclidean {}; mute {}",opts.scene,data[o+4]+1,data[o+5],data[o+7],euclidean_label(data[o+5] as usize,data[15738+i] as usize,data[15774+i] as usize),data[o+9]!=0).unwrap();
 let grid=cells(data,t,opts);
 for row in grid.chunks_exact(8){out.push_str("  ");for c in row{if !c.selected{out.push_str("     ");continue;}
 let symbol=match(c.in_motion,c.events>0,opts.ascii){(true,true,true)=>"#",(true,false,true)=>".",(false,true,true)=>"+",(false,false,true)=>"-",(true,true,false)=>"■",(true,false,false)=>"·",(false,true,false)=>"▧",(false,false,false)=>"░"};
 if color{let style=match(c.in_motion,c.events>0){(true,true)=>"\x1b[97;41m",(true,false)=>"\x1b[37;40m",(false,true)=>"\x1b[30;47m",(false,false)=>"\x1b[90;40m"};out.push_str(style);}
 write!(out,"{:02}{symbol} ",c.step).unwrap();if color{out.push_str("\x1b[0m");}out.push(' ');
 }out.push('\n');}out.push('\n');
 }Ok(out)
}
/// JSON view compiles known fields in memory, never writes an archive/device.
pub fn render_json(doc:&serde_json::Value,opts:&Options)->Result<String,String>{let(_,raw)=document::import(doc)?;render(&raw,opts)}
#[cfg(test)]mod tests{use super::*;
 #[test]fn selection(){let args=vec!["--track".into(),"2".into(),"--steps".into(),"3-7".into()];assert_eq!(Options::parse(&args).unwrap(),Options{track:Some(2),first_step:3,last_step:7,..Options::default()});for a in [vec!["--track","7"],vec!["--steps","7-3"],vec!["--steps","0"],vec!["--what","1"],vec!["--track"]]{assert!(Options::parse(&a.iter().map(|s|s.to_string()).collect::<Vec<_>>()).is_err());}}
 #[test]fn notes_and_empty(){assert_eq!(pitch(48),"C3 (48)");assert_eq!(pitch(60),"C4 (60)");assert_eq!(detail(&[0,255,255,255,255,255]),("Empty",String::new()));assert!(detail(&[0x90,60,96,255,106,0]).1.contains("106 ticks"));}

 #[test]fn grid_cells(){let mut d=vec![0;SONG_SIZE];d[484]=62;d[485]=4;d[1824]=0xb0;let opts=Options{grid:true,track:Some(1),color:"never".into(),ascii:true,..Options::default()};let grid=cells(&d,1,&opts);assert!(grid[0].in_motion && grid[1].in_motion && grid[62].in_motion && grid[63].in_motion);assert!(!grid[2].in_motion);assert_eq!(grid[0].events,1);let text=render_grid(&d,&opts).unwrap();assert!(text.contains("01#"));assert!(text.contains("03-"));assert!(!text.contains('\x1b'));}
 #[test]fn grid_options(){let args=["--layout","grid","--scene","2","--color","never","--ascii"].map(String::from);let o=Options::parse(&args).unwrap();assert!(o.grid&&o.ascii);assert_eq!(o.scene,2);assert!(Options::parse(&["--scene".into(),"7".into()]).is_err());}

 #[test]fn euclidean_display(){assert!(euclidean_label(4,82,7).starts_with("4:3:3"));assert!(euclidean_label(4,100,7).starts_with("Pattern"));assert!(euclidean_label(1,0,63).starts_with("1:0:0"));}

 #[test]fn grid_red_palette(){let mut d=vec![0;SONG_SIZE];for i in 0..36{d[480+16*i+5]=4;}d[1824]=0x90;let opts=Options{grid:true,color:"always".into(),..Options::default()};let text=render_grid(&d,&opts).unwrap();assert!(text.contains("\x1b[97;41m01■"));assert!(!text.contains("\x1b[97;44m"));}
}
