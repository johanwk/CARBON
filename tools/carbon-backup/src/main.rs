// SPDX-License-Identifier: GPL-3.0-or-later
use carbon_backup::*;
use midir::{Ignore,MidiInput,MidiOutput};
use std::{env,fs::{File,OpenOptions},path::Path,sync::mpsc,time::{Duration,Instant,SystemTime,UNIX_EPOCH}};

fn run()->Result<(),String>{
    let args:Vec<String>=env::args().collect();
    if args.get(1).map(String::as_str)==Some("verify") && args.len()==3 {
        let mut file=File::open(&args[2]).map_err(|e|e.to_string())?;
        let mut magic=[0u8;8];
        std::io::Read::read_exact(&mut file,&mut magic).map_err(|e|e.to_string())?;
        let file=File::open(&args[2]).map_err(|e|e.to_string())?;
        if &magic==b"CARBNSG1" {
            let (m,_)=verify_song_archive(file)?;
            println!("Verified song slot{}: {}",m["slot"],m["song_sha256"]);
        } else {
            let (m,_)=verify_archive(file)?;println!("Verified64 slots: {}",m["bank_sha256"]);
        }
        return Ok(());
    }
    if args.get(1).map(String::as_str)==Some("restore-song") {
        // Explicitly gated hardware test scope: selected spare slot30 only.
        if args.len()!=10 || args[7]!="--stopped" || args[8]!="--firmware-0623fee" {
            return Err("Usage: restore-song SOURCE TARGET_BACKUP 30 RECOVERY_FILE PORT_NAME --stopped --firmware-0623fee APPROVAL_TOKEN".into());
        }
        let (_,source)=verify_song_archive(File::open(&args[2]).map_err(|e|e.to_string())?)?;
        let (target,base)=verify_song_archive(File::open(&args[3]).map_err(|e|e.to_string())?)?;
        let slot=args[4].parse::<usize>().map_err(|_|"Invalid target slot")?;
        if slot!=30 || target["slot"]!=30 {return Err("Initial write test restricted to backed-up slot30".into());}
        let plan=restore::Plan::new(slot,base,source,restore::Mode::Replace)?;
        println!("{}",serde_json::to_string_pretty(&plan.summary()).map_err(|e|e.to_string())?);
        if args[9]!=plan.approval_token() {return Err("Exact replacement approval token required".into());}
        if Path::new(&args[5]).exists(){return Err("Recovery output exists".into());}
        let link=midi::MidiLink::open(&args[6],&args[6])?;
        let mut device=transfer::Adapter{link,read_timeout:Duration::from_secs(3),stage_timeout:Duration::from_secs(3),commit_timeout:Duration::from_secs(30)};
        restore::execute(&mut device,&plan,&args[9],true,true,|raw|{
            let timestamp=SystemTime::now().duration_since(UNIX_EPOCH).map_err(|e|e.to_string())?.as_secs();
            let m=song_manifest(raw,30,&args[6],&args[6],timestamp)?;
            let mut file=OpenOptions::new().write(true).create_new(true).open(&args[5]).map_err(|e|e.to_string())?;
            write_song_archive(&mut file,raw,&m)?;file.sync_all().map_err(|e|e.to_string())?;drop(file);
            verify_song_archive(File::open(&args[5]).map_err(|e|e.to_string())?)?;
            eprintln!("Fresh slot30 recovery backup saved and verified before writes");Ok(())
        })?;
        println!("Slot30 replacement and full readback match succeeded. Reload slot30 from panel.");
        return Ok(());
    }
    if args.get(1).map(String::as_str)==Some("plan-restore") {
        if args.len()!=6 {return Err("Usage: plan-restore SOURCE_SONG_ARCHIVE TARGET_BACKUP TARGET_SLOT replace|patch (offline only)".into());}
        let (_,after)=verify_song_archive(File::open(&args[2]).map_err(|e|e.to_string())?)?;
        let (base,before)=verify_song_archive(File::open(&args[3]).map_err(|e|e.to_string())?)?;
        let slot=args[4].parse::<usize>().map_err(|_|"Invalid target slot")?;
        if base["slot"]!=slot {return Err("Target backup belongs to a different slot".into());}
        let mode=match args[5].as_str(){"replace"=>restore::Mode::Replace,"patch"=>restore::Mode::Patch,_=>return Err("Choose replace or patch".into())};
        let plan=restore::Plan::new(slot,before,after,mode)?;
        println!("{}",serde_json::to_string_pretty(&plan.summary()).map_err(|e|e.to_string())?);
        return Ok(());
    }
    let mut input=MidiInput::new("CARBON read-only backup").map_err(|e|e.to_string())?;
    input.ignore(Ignore::None);
    let output=MidiOutput::new("CARBON read requests only").map_err(|e|e.to_string())?;
    if args.get(1).map(String::as_str)==Some("ports") && args.len()==2 {
        for p in input.ports(){println!("IN  {}",input.port_name(&p).map_err(|e|e.to_string())?);}
        for p in output.ports(){println!("OUT {}",output.port_name(&p).map_err(|e|e.to_string())?);}
        return Ok(());
    }
    let single=args.get(1).map(String::as_str)==Some("backup-song");
    let stop_index=if single {6} else {5};
    if !(args.len()==stop_index+1 || args.len()==stop_index+3)
        || (!single && args.get(1).map(String::as_str)!=Some("backup-bank"))
        || args[stop_index]!="--stopped" {
        return Err("Usage: carbon-backup ports | verify FILE | backup-bank FILE INPUT OUTPUT --stopped [--pause-ms N] | backup-song SLOT FILE INPUT OUTPUT --stopped [--pause-ms N]\nStop transport and avoid panel edits/save/load. No restore command.".into());
    }
    let slot=if single {Some(args[2].parse::<usize>().map_err(|_|"Invalid panel slot")?)} else {None};
    let range=if let Some(slot)=slot {song_range(slot)?} else {0..BANK_SIZE};
    let file_index=if single {3} else {2};
    let input_index=file_index+1;let output_index=file_index+2;
    let pause_ms=if args.len()==stop_index+3 {
        if args[stop_index+1]!="--pause-ms" {return Err("Expected --pause-ms".into());}
        args[stop_index+2].parse::<u64>().map_err(|_|"Invalid pause milliseconds")?
    } else {5};
    if pause_ms>100 {return Err("Pause must be0..100ms".into());}
    if Path::new(&args[file_index]).exists(){return Err("Output exists; refusing overwrite".into());}
    let inp=input.ports().into_iter().filter(|p|input.port_name(p).ok().as_ref()==Some(&args[input_index])).collect::<Vec<_>>();
    let out=output.ports().into_iter().filter(|p|output.port_name(p).ok().as_ref()==Some(&args[output_index])).collect::<Vec<_>>();
    if inp.len()!=1 || out.len()!=1 {return Err("Port names must match exactly one input and output; use CARBON USB Port1".into());}
    let (tx,rx)=mpsc::sync_channel(256);
    let mut frame=Vec::<u8>::new();
    let _connection=input.connect(&inp[0],"CARBON reads",move|_,bytes,_|{
        for &b in bytes {
            if b>=0xf8 {continue;}
            if b==0xf0 {frame.clear();frame.push(b);continue;}
            if frame.is_empty(){continue;}
            if frame.len()>=200 {frame.clear();continue;}
            frame.push(b);
            if b==0xf7 {let _=tx.try_send(std::mem::take(&mut frame));}
        }
    },()).map_err(|e|e.to_string())?;
    let mut connection=output.connect(&out[0],"CARBON read requests").map_err(|e|e.to_string())?;
    let mut capture=|pass:usize|->Result<Vec<u8>,String>{
        let mut data=Vec::with_capacity(range.len());
        let started=Instant::now();
        for address in range.clone().step_by(64){
            // The ONLY outbound command anywhere in this executable is0x70.
            connection.send(&request(address)?).map_err(|e|e.to_string())?;
            let deadline=Instant::now()+Duration::from_secs(3);
            loop {
                let remaining=deadline.saturating_duration_since(Instant::now());
                if remaining.is_zero(){return Err(format!("Timeout at{address:06x}; abort, no retry"));}
                let p=rx.recv_timeout(remaining).map_err(|e|format!("Read aborted at{address:06x}: {e}"))?;
                if let Some(bytes)=response(&p,address)? {data.extend(bytes);break;}
            }
            if (address+64)%SONG_SIZE==0 {eprintln!("Pass{pass}/2 slot{}/64",(address+64)/SONG_SIZE);}
            if pause_ms>0 {std::thread::sleep(Duration::from_millis(pause_ms));}
        }
        eprintln!("Pass{pass} completed in{:.1}s; {:.0} bytes/s",started.elapsed().as_secs_f64(),range.len() as f64/started.elapsed().as_secs_f64());
        Ok(data)
    };
    eprintln!("Read-only capture: two passes,{} bytes each; leave CARBON undisturbed.",range.len());
    eprintln!("Source slots: {}..{}",range.start/SONG_SIZE+1,range.end/SONG_SIZE);
    eprintln!("Extra pause after each matched reply: {pause_ms}ms");
    let started=Instant::now();
    let a=capture(1)?;let b=capture(2)?;
    if a!=b {return Err("Saved song data changed between captures; no backup created".into());}
    let now=SystemTime::now().duration_since(UNIX_EPOCH).map_err(|e|e.to_string())?.as_secs();
    let mut m=if let Some(slot)=slot {song_manifest(&a,slot,&args[input_index],&args[output_index],now)?}
        else {manifest(&a,&args[input_index],&args[output_index],now)?};
    m["pause_ms"]=serde_json::json!(pause_ms);
    m["capture_seconds"]=serde_json::json!(started.elapsed().as_secs_f64());
    let mut file=OpenOptions::new().write(true).create_new(true).open(&args[file_index]).map_err(|e|e.to_string())?;
    if single {write_song_archive(&mut file,&a,&m)?;} else {write_archive(&mut file,&a,&m)?;}file.sync_all().map_err(|e|e.to_string())?;drop(file);
    let file=File::open(&args[file_index]).map_err(|e|e.to_string())?;
    if single {verify_song_archive(file)?;} else {verify_archive(file)?;}
    println!("Saved and verified {}\nData SHA256 {}",args[file_index],hash(&a));Ok(())
}
fn main(){if let Err(e)=run(){eprintln!("ERROR: {e}");std::process::exit(1);}}
