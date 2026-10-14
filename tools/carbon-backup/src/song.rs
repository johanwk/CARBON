// SPDX-License-Identifier: GPL-3.0-or-later
//! Stored1.34 fields; offsets checked against compiled song.c layout.
use crate::SONG_SIZE;
use serde_json::{json,Value};
#[derive(Clone,Copy)] enum Kind{U8,I8,I16,I32,F32}
struct Field {name:String,offset:usize,kind:Kind,min:f64,max:f64}
fn fields()->Vec<Field>{
 let mut f=vec![];
 let mut add=|name:String,offset,kind,min,max|f.push(Field{name,offset,kind,min,max});
 for (name,o,k,min,max) in [("tempo",4,Kind::F32,30.,300.1),("swing",8,Kind::I8,50.,80.),
 ("metronome",9,Kind::I8,0.,84.),("key_velocity_scale",10,Kind::I16,-100.,100.),
 ("cv_bend_range",12,Kind::I8,1.,12.),("cv_pairing",13,Kind::I8,0.,3.)]{add(name.into(),o,k,min,max);}
 for i in 0..4{add(format!("cv{}.mode",i+1),14+i,Kind::I8,-2.,120.);add(format!("cv{}.scaling",i+1),18+i,Kind::I8,0.,1.);add(format!("cv{}.calibration",i+1),22+i,Kind::I8,-100.,100.);add(format!("cv{}.offset",i+1),15654+2*i,Kind::I16,-450.,450.);}
 for i in 0..5{add(format!("clock_output{}.rate_index",i+1),28+4*i,Kind::I32,0.,9.);}
 for t in 0..6{let o=432+8*t;
 for j in 0..2{add(format!("track{}.output{}.program",t+1,j+1),o+j,Kind::I8,-1.,127.);add(format!("track{}.output{}.port",t+1,j+1),o+2+j,Kind::I8,-1.,4.);add(format!("track{}.output{}.channel_zero_based",t+1,j+1),o+4+j,Kind::I8,0.,15.);}
 add(format!("track{}.key_split",t+1),o+6,Kind::I8,0.,2.);add(format!("track{}.type",t+1),o+7,Kind::I8,0.,1.);}
 let scene_fields=[("step_length_index",0,Kind::I8,0.,22.),("scale_index",1,Kind::I8,0.,15.),
 ("transpose",2,Kind::I8,-24.,24.),("bias_source_zero_based",3,Kind::I8,-1.,5.),
 ("motion_start_zero_based",4,Kind::I8,0.,63.),("motion_length",5,Kind::I8,1.,64.),
 ("gate_raw",6,Kind::U8,1.,255.),("pattern",7,Kind::I8,0.,31.),
 ("reverse",8,Kind::I8,0.,1.),("mute",9,Kind::I8,0.,1.),
 ("arp_speed_index",10,Kind::I8,0.,16.),("arp_type",11,Kind::I8,0.,36.),
 ("arp_gate_ticks",12,Kind::I16,1.,384.),("arp_enabled",14,Kind::I16,0.,1.)];
 for s in 0..6{add(format!("scene{}.root",s+1),15811+s,Kind::I8,-2.,11.);for t in 0..6{let i=s*6+t;
 for (name,o,k,min,max) in scene_fields{add(format!("scene{}.track{}.{}",s+1,t+1,name),480+16*i+o,k,min,max);}
 add(format!("scene{}.track{}.density",s+1,t+1),15738+i,Kind::U8,0.,100.);add(format!("scene{}.track{}.rotation",s+1,t+1),15774+i,Kind::U8,0.,63.);
 for j in 0..2{add(format!("scene{}.track{}.output{}.program",s+1,t+1,j+1),15666+2*i+j,Kind::I8,-1.,127.);}}}
 for t in 0..6{for s in 0..64{let o=1056+2*(t*64+s);add(format!("track{}.step{}.shared_delay_ticks",t+1,s+1),o,Kind::U8,0.,96.);add(format!("track{}.step{}.ratchet",t+1,s+1),o+1,Kind::U8,1.,8.);}}
 for(name,o,k,min,max)in[("midi_remote",15648,Kind::U8,0.,1.),("metronome_length_ms",15649,Kind::U8,5.,160.),
 ("clock_source",15650,Kind::I8,-1.,2.),("autolive",15662,Kind::U8,0.,1.),("scene_sync",15663,Kind::U8,0.,1.),
 ("magic_range",15664,Kind::U8,0.,36.),("magic_chance",15665,Kind::U8,1.,100.),("song_root",15810,Kind::I8,-1.,11.)]{add(name.into(),o,k,min,max);}
 f
}
fn width(k:Kind)->usize{match k{Kind::U8|Kind::I8=>1,Kind::I16=>2,_=>4}}
fn value(d:&[u8],f:&Field)->f64{let o=f.offset;match f.kind{Kind::U8=>d[o] as f64,Kind::I8=>(d[o] as i8) as f64,Kind::I16=>i16::from_le_bytes(d[o..o+2].try_into().unwrap()) as f64,Kind::I32=>i32::from_le_bytes(d[o..o+4].try_into().unwrap()) as f64,Kind::F32=>f32::from_le_bytes(d[o..o+4].try_into().unwrap()) as f64}}
pub fn validate(d:&[u8])->Result<(),String>{
 if d.len()!=SONG_SIZE{return Err("Wrong song size".into());}
 for f in fields(){let v=value(d,&f);if !v.is_finite()||v<f.min||v>f.max{return Err(format!("Invalid{}={v}",f.name));}
 if f.name=="metronome" && v>2. && v<24.{return Err("Invalid metronome mode".into());}}
 // NULL song-list entries have opaque/stale payload. Active entries use UI limits.
 for i in 0..64{let o=48+6*i;let scene=d[o] as i8;if scene == -1{continue;}
 let beats=i16::from_le_bytes([d[o+2],d[o+3]]);let transpose=d[o+4] as i8;
 if !(0..=7).contains(&scene)||!(1..=256).contains(&beats)||!(-24..=24).contains(&transpose){return Err(format!("Invalid song-list entry{}",i+1));}}
 Ok(())
}
pub fn diff(a:&[u8],b:&[u8])->Result<Value,String>{
 if a.len()!=SONG_SIZE||b.len()!=SONG_SIZE{return Err("Wrong song size".into());}
 let mut covered=vec![false;SONG_SIZE];let mut changes=vec![];
 for f in fields(){let o=f.offset;let n=width(f.kind);covered[o..o+n].fill(true);
 if a[o..o+n]!=b[o..o+n]{changes.push(json!({"field":f.name,"before":value(a,&f),"after":value(b,&f),"offset":o}));}}
 for i in 0..64{let o=48+6*i;for(name,j,n)in[("scene",0,1),("length_beats",2,2),("transpose",4,1)]{covered[o+j..o+j+n].fill(true);if a[o+j..o+j+n]!=b[o+j..o+j+n]{let decode=|d:&[u8]|if n==2{i16::from_le_bytes([d[o+j],d[o+j+1]]) as i32}else{d[o+j] as i8 as i32};changes.push(json!({"field":format!("song_list{}.{}",i+1,name),"before":decode(a),"after":decode(b),"inactive_payload":a[o]==255&&b[o]==255}));}}}
 covered[1824..15648].fill(true);
 let opaque:Vec<usize>=(0..SONG_SIZE).filter(|i|a[*i]!=b[*i]&&!covered[*i]).collect();
 Ok(json!({"field_changes":changes,"opaque_changes":opaque}))
}
#[cfg(test)] mod tests{use super::*;#[test]fn bounds_and_diffs(){let d=vec![0;SONG_SIZE];assert!(validate(&d).is_err());let mut b=d.clone();b[15810]=5;assert_eq!(diff(&d,&b).unwrap()["field_changes"][0]["field"],"song_root");b[16000]=1;assert_eq!(diff(&d,&b).unwrap()["opaque_changes"],json!([16000]));}}
