// SPDX-License-Identifier: GPL-3.0-or-later
//! MIDI connection for explicit restore sessions. Never opens automatically.
use midir::{Ignore,MidiInput,MidiInputConnection,MidiOutput,MidiOutputConnection};
use std::{sync::mpsc::{self,Receiver},time::Instant};
use crate::transfer::Link;
pub struct MidiLink { _input:MidiInputConnection<()>, output:MidiOutputConnection, rx:Receiver<Vec<u8>> }
impl MidiLink {
 pub fn open(input_name:&str,output_name:&str)->Result<Self,String>{
    // Initial hardware-write scope: CARBON USB device port1 only.
    if input_name!="CARBON Port 1" || output_name!="CARBON Port 1" {return Err("Restore requires exact CARBON Port1 endpoints".into());}
    let mut input=MidiInput::new("CARBON controlled restore").map_err(|e|e.to_string())?;input.ignore(Ignore::None);
    let output=MidiOutput::new("CARBON controlled restore").map_err(|e|e.to_string())?;
    let ins=input.ports().into_iter().filter(|p|input.port_name(p).ok().as_deref()==Some(input_name)).collect::<Vec<_>>();
    let outs=output.ports().into_iter().filter(|p|output.port_name(p).ok().as_deref()==Some(output_name)).collect::<Vec<_>>();
    if ins.len()!=1 || outs.len()!=1 {return Err("Choose unique connected CARBON Port1 endpoints".into());}
    let(tx,rx)=mpsc::sync_channel(256);let mut frame=Vec::new();
    let connection=input.connect(&ins[0],"CARBON restore input",move|_,bytes,_|{
        for &b in bytes {if b>=0xf8{continue;}if b==0xf0{frame.clear();frame.push(b);continue;}
        if frame.is_empty(){continue;}if frame.len()>=200{frame.clear();continue;}frame.push(b);
        if b==0xf7{let _=tx.try_send(std::mem::take(&mut frame));}}
    },()).map_err(|e|e.to_string())?;
    let output=output.connect(&outs[0],"CARBON restore output").map_err(|e|e.to_string())?;
    Ok(Self{_input:connection,output,rx})
 }
}
impl Link for MidiLink {
 fn send(&mut self,p:&[u8])->Result<(),String>{self.output.send(p).map_err(|e|e.to_string())}
 fn receive_until(&mut self,deadline:Instant)->Result<Option<Vec<u8>>,String>{
    match self.rx.recv_timeout(deadline.saturating_duration_since(Instant::now())) {
        Ok(p)=>Ok(Some(p)),Err(mpsc::RecvTimeoutError::Timeout)=>Ok(None),Err(e)=>Err(e.to_string())
    }
 }
}
