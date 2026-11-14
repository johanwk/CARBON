// SPDX-License-Identifier: GPL-3.0-or-later
use carbon_backup::*;
use midir::{Ignore,MidiInput,MidiOutput};
use std::{env,fs::{File,OpenOptions},path::Path,sync::mpsc,time::{Duration,Instant,SystemTime,UNIX_EPOCH}};

const HELP:&str = "CARBON song tools - MIDI import, JSON editing and safe single-song transfer

Everyday song transfers (JSON files):
  backup song SLOT FILE.json             Save one song from CARBON
  upload song SLOT FILE.json             Replace one song on CARBON
  Example: backup song 37 song37.json
           upload song 37 song37.json
  CARBON Port 1 is automatic. One prompt covers stopped transport and, for
  upload, community firmware 1.36. Recovery backup and readback are automatic.
  After upload, LOAD the song from the panel. No token or flags needed.

Offline commands:
  analyse-midi FILE [--json]              Inspect MIDI tracks, bars and timing
  plan-midi FILE [selection options]      Preview approximate note conversion
  import-midi MIDI TEMPLATE OUTPUT --output-port N [selection options]
                                         Create song JSON; replaces musical content
  validate-json FILE                     Check JSON and binary compilation
  import-json JSON BACKUP                Compile a single-song archive
  export-json BACKUP JSON                Export editable song JSON
  view FILE [--track N] [--steps A-B]     Inspect stored notes/grid
  verify BACKUP                          Verify archive integrity
  plan-restore SOURCE BASE SLOT replace  Preview single-song replacement

Device commands (explicit invocation only):
  download SLOT FILE.json               Read a saved song as JSON
  upload FILE.json SLOT                 Replace one song; automatic recovery backup
    Default CARBON Port 1; interactive stopped/firmware confirmation. No flags needed.

Advanced device commands:
  ports
  backup-song SLOT FILE INPUT OUTPUT --stopped [--pause-ms N]
  backup-bank FILE INPUT OUTPUT --stopped [--pause-ms N]
  restore-song SOURCE BASE SLOT RECOVERY PORT --stopped --firmware-community-1.36 --confirm|TOKEN
    WRITES one song slot1..64. Requires reviewed plan and fresh recovery backup.

MIDI selection: --start-bar N --bars N --tracks 2,3|NAME --step-length auto|96
Output ports: 0 DIN1, 1 DIN2, 3 USB-host, 4 USB-device (CV unsupported).
Use --step-length 96 --bars 16 for a quarter-grid opening excerpt in 4/4.
Conversion may drop/shift notes; inspect metadata.extensions.midi_import.
Template must be schema3/1.36 (or explicit legacy schema2/1.35). Source programs/expression are not reproduced.
See tools/carbon-backup/README.md for policies and transfer precautions.
";

fn confirm_transfer(action:&str,slot:usize)->Result<(),String>{
    use std::io::{IsTerminal,Write};
    if !std::io::stdin().is_terminal(){return Err("Confirmation requires an interactive terminal; no device accessed".into());}
    println!("Keep CARBON stopped and do not edit, save or load songs until completion.");
    if action=="replace"{
        println!("Requires installed community firmware 1.36-jwk-rc1 (or documented compatible snp3).\nFirmware is NOT detected automatically. Older or official firmware is not supported here.\nThe entire saved song will be overwritten. A recovery backup will be saved first.\nKeep power connected; an interrupted write may leave the target damaged.");
    }else{println!("Reads saved flash only, not unsaved edits. This does not change CARBON.");}
    print!("\nTo confirm these conditions, type '{action} {slot}' (anything else cancels): ");
    std::io::stdout().flush().map_err(|e|e.to_string())?;
    let mut line=String::new();std::io::stdin().read_line(&mut line).map_err(|e|e.to_string())?;
    if !song_transfer::confirmation(action,slot,&line){return Err("Cancelled; no device accessed".into());}
    Ok(())
}
fn simple_transfer(args:&[String])->Result<(),String>{
    use std::io::Write;
    let (upload,slot,filename)=song_transfer::arguments(&args[1..])?;
    let path=Path::new(&filename);
    let raw=if upload{
        let text=std::fs::read_to_string(path).map_err(|e|e.to_string())?;
        let data=song_transfer::validate_upload(&text)?;
        let notes=data[1824..15648].chunks_exact(6).filter(|e|e[0]==0x90).count();
        println!("Upload {} -> song{slot} via {}\nValidated 1.36 song: {notes} stored Notes; SHA256 {}",path.display(),song_transfer::PORT,hash(&data));
        Some(data)
    }else{
        if path.exists(){return Err("Output exists; choose a new JSON filename".into());}
        println!("Download song{slot} -> {} via {}",path.display(),song_transfer::PORT);None
    };
    confirm_transfer(if upload{"replace"}else{"download"},slot)?;
    // Reserve local output/recovery paths before opening the device.
    let mut output=if upload{None}else{Some(OpenOptions::new().write(true).create_new(true).open(path).map_err(|e|e.to_string())?)};
    let recovery=if upload{
        let root=env::current_dir().map_err(|e|e.to_string())?.join("carbon-recovery");
        std::fs::create_dir_all(&root).map_err(|e|e.to_string())?;
        let stamp=SystemTime::now().duration_since(UNIX_EPOCH).map_err(|e|e.to_string())?.as_nanos();
        let session=root.join(format!("song{slot}-{stamp}-{}",std::process::id()));
        std::fs::create_dir(&session).map_err(|e|e.to_string())?;
        let backup=session.join("before.backup");
        println!("Recovery backup: {}",backup.display());Some(backup)
    }else{None};
    let link=midi::MidiLink::open(song_transfer::PORT,song_transfer::PORT)?;
    let mut device=transfer::Adapter{link,read_timeout:Duration::from_secs(3),stage_timeout:Duration::from_secs(3),commit_timeout:Duration::from_secs(30)};
    if let Some(raw)=raw{
        let backup=recovery.ok_or("Missing recovery path")?;
        let result=song_transfer::upload(&mut device,slot,raw,true,|before|{
            let timestamp=SystemTime::now().duration_since(UNIX_EPOCH).map_err(|e|e.to_string())?.as_secs();
            let manifest=song_manifest(before,slot,song_transfer::PORT,song_transfer::PORT,timestamp)?;
            let mut file=OpenOptions::new().write(true).create_new(true).open(&backup).map_err(|e|e.to_string())?;
            write_song_archive(&mut file,before,&manifest)?;file.sync_all().map_err(|e|e.to_string())?;drop(file);
            let (_,saved)=verify_song_archive(File::open(&backup).map_err(|e|e.to_string())?)?;
            if saved!=before{return Err("Recovery file differs from fresh target; no write permitted".into());}
            eprintln!("Recovery backup verified. Writing song{slot}...");Ok(())
        })?;
        println!("Song{slot} uploaded and full readback verified. SHA256 {result}\nLOAD song{slot} from the panel; do not save stale RAM over it.");
    }else{
        let doc=song_transfer::download(&mut device,slot,true)?;
        let bytes=serde_json::to_vec_pretty(&doc).map_err(|e|e.to_string())?;
        let file=output.as_mut().ok_or("Missing output file")?;
        file.write_all(&bytes).map_err(|e|e.to_string())?;file.sync_all().map_err(|e|e.to_string())?;
        let saved=std::fs::read_to_string(path).map_err(|e|e.to_string())?;
        document::validate_text(&saved)?;
        if saved.as_bytes()!=bytes{return Err("Downloaded JSON failed disk readback comparison".into());}
        println!("Saved and validated {} (two matching reads). CARBON unchanged.",path.display());
    }
    Ok(())
}

fn run()->Result<(),String>{
    let args:Vec<String>=env::args().collect();
    if args.len()==1 || matches!(args.get(1).map(String::as_str),Some("--help"|"-h"|"help")) || args.get(2).is_some_and(|a|a=="--help" || a=="-h") {
        println!("{HELP}"); return Ok(());
    }
    if matches!(args[1].as_str(),"backup"|"download"|"upload"){return simple_transfer(&args);}
    if !["import-midi","plan-midi","analyse-midi","validate-json","view","verify","export-json","import-json","restore-song","plan-restore","ports","backup-song","backup-bank"].contains(&args[1].as_str()){
        return Err(format!("Unknown command {:?}; use --help",args[1]));
    }

    if args.get(1).map(String::as_str)==Some("validate-json") {
        if args.len()!=3{return Err("Usage: validate-json FILE (offline; no output file or device access)".into());}
        let text=std::fs::read_to_string(&args[2]).map_err(|e|e.to_string())?;
        println!("{}",document::validate_text(&text)?);
        return Ok(());
    }

    if args.get(1).map(String::as_str)==Some("import-midi") {
        if args.len()<7{return Err("Usage: import-midi MIDI TEMPLATE_JSON OUTPUT_JSON --output-port 0|1|3|4 [plan-midi options]; REPLACES all shared tracks/scenes offline".into());}
        let mut port=None;let mut planning=vec![];
        if (args.len()-5)%2!=0{return Err("Options require values".into());}
        for pair in args[5..].chunks_exact(2){if pair[0]=="--output-port"{if port.is_some(){return Err("Repeated output port".into());}port=Some(pair[1].parse::<u8>().map_err(|_|"Invalid output port")?);}else{planning.extend_from_slice(pair);}}
        let port=port.ok_or("Explicit --output-port required")?;
        let options=midi_plan::Options::parse(&planning)?;let source=smf::read_file(Path::new(&args[2]))?;
        let template=json::parse(&std::fs::read_to_string(&args[3]).map_err(|e|e.to_string())?)?;
        let doc=midi_compile::compile(&source,&options,&template,port)?;
        let text=serde_json::to_vec_pretty(&doc).map_err(|e|e.to_string())?;
        let mut file=OpenOptions::new().write(true).create_new(true).open(&args[4]).map_err(|e|e.to_string())?;
        std::io::Write::write_all(&mut file,&text).map_err(|e|e.to_string())?;file.sync_all().map_err(|e|e.to_string())?;
        println!("Compiled MIDI to template-profile JSON offline; {} notes. See metadata.extensions.midi_import for full losses/policies. No upload.",doc["metadata"]["extensions"]["midi_import"]["compiled_notes"]);return Ok(());
    }

    if args.get(1).map(String::as_str)==Some("plan-midi") {
        if args.len()<3{return Err("Usage: plan-midi FILE [--start-bar N] [--bars N] [--tracks NUMBERS_OR_NAMES] [--step-length auto|TICKS] (offline JSON plan only)".into());}
        let options=midi_plan::Options::parse(&args[3..])?;
        let source=smf::read_file(Path::new(&args[2]))?;
        let text=serde_json::to_string_pretty(&midi_plan::plan(&source,&options)?).map_err(|e|e.to_string())?;
        match std::io::Write::write_all(&mut std::io::stdout().lock(),text.as_bytes()){
            Ok(())=>{},Err(e) if e.kind()==std::io::ErrorKind::BrokenPipe=>{},Err(e)=>return Err(e.to_string())
        }
        return Ok(());
    }

    if args.get(1).map(String::as_str)==Some("analyse-midi") {
        if !(args.len()==3 || (args.len()==4 && args[3]=="--json")) {return Err("Usage: analyse-midi FILE [--json] (offline)".into());}
        let source=smf::read_file(Path::new(&args[2]))?;
        let report=source.report();
        let text=if args.len()==4 {serde_json::to_string_pretty(&report).map_err(|e|e.to_string())?}else{smf::render(&report)};
        match std::io::Write::write_all(&mut std::io::stdout().lock(),text.as_bytes()){
            Ok(())=>{},Err(e) if e.kind()==std::io::ErrorKind::BrokenPipe=>{},Err(e)=>return Err(e.to_string())
        }
        return Ok(());
    }

    if args.get(1).map(String::as_str)==Some("view") {
        if args.len()<3{return Err("Usage: view FILE [--track 1..6] [--steps N|A-B] (JSON or single-song archive; offline)".into());}
        let opts=view::Options::parse(&args[3..])?;
        let bytes=std::fs::read(&args[2]).map_err(|e|e.to_string())?;
        let text=if bytes.starts_with(b"CARBNSG1"){
            let(_,raw)=verify_song_archive(&bytes[..])?;view::render(&raw,&opts)?
        }else{
            let text=std::str::from_utf8(&bytes).map_err(|_|"Expected JSON or single-song archive")?;
            view::render_json(&json::parse(text)?,&opts)?
        };
        // Broken pipe (e.g. view | head) is normal termination, not a panic.
        match std::io::Write::write_all(&mut std::io::stdout().lock(),text.as_bytes()){
            Ok(())=>{},Err(e) if e.kind()==std::io::ErrorKind::BrokenPipe=>{},Err(e)=>return Err(e.to_string())
        }
        return Ok(());
    }
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
    if args.get(1).map(String::as_str)==Some("export-json") && args.len()==4 {
        let (m,data)=verify_song_archive(File::open(&args[2]).map_err(|e|e.to_string())?)?;
        let slot=m["slot"].as_u64().ok_or("Missing slot")? as usize;
        let doc=document::export_archive(&m,&data,slot)?;
        let mut file=OpenOptions::new().write(true).create_new(true).open(&args[3]).map_err(|e|e.to_string())?;
        std::io::Write::write_all(&mut file,serde_json::to_string_pretty(&doc).map_err(|e|e.to_string())?.as_bytes()).map_err(|e|e.to_string())?;
        file.sync_all().map_err(|e|e.to_string())?;println!("Exported JSON offline; no device access");return Ok(());
    }
    if args.get(1).map(String::as_str)==Some("import-json") && args.len()==4 {
        let text=std::fs::read_to_string(&args[2]).map_err(|e|e.to_string())?;
        let doc=json::parse(&text)?;
        let(slot,data)=document::import(&doc)?;
        let manifest=serde_json::json!({"format":"CARBNSG1","schema":1,"slot":slot,"address":song_range(slot)?.start,
            "song_bytes":SONG_SIZE,"song_sha256":hash(&data),"matching_capture_passes":0,"origin":"json_compilation","song_magic_valid":true,"document_metadata":doc["metadata"]});
        let mut file=OpenOptions::new().write(true).create_new(true).open(&args[3]).map_err(|e|e.to_string())?;
        write_song_archive(&mut file,&data,&manifest)?;file.sync_all().map_err(|e|e.to_string())?;drop(file);
        verify_song_archive(File::open(&args[3]).map_err(|e|e.to_string())?)?;
        println!("Compiled offline song image SHA256 {}",hash(&data));return Ok(());
    }
    if args.get(1).map(String::as_str)==Some("restore-song") {
        // Explicitly selected hardware test slots; backup identity must match.
        if args.len()!=10 || args[7]!="--stopped" || !["--firmware-9c91ab0","--firmware-1.36-gli3","--firmware-community-1.36"].contains(&args[8].as_str()) {
            return Err("Usage: restore-song SOURCE TARGET_BACKUP TARGET_SLOT RECOVERY_FILE PORT_NAME --stopped --firmware-community-1.36 APPROVAL_TOKEN|--confirm".into());
        }
        let (_,source)=verify_song_archive(File::open(&args[2]).map_err(|e|e.to_string())?)?;
        let (target,base)=verify_song_archive(File::open(&args[3]).map_err(|e|e.to_string())?)?;
        let slot=args[4].parse::<usize>().map_err(|_|"Invalid target slot")?;
        restore::validate_hardware_target(slot,&target)?;
        restore::validate_hardware_source(&source, &args[8])?;
        let plan=restore::Plan::new(slot,base,source,restore::Mode::Replace)?;
        println!("{}",plan.brief());
        if Path::new(&args[5]).exists(){return Err("Recovery output exists; choose a new recovery filename".into());}
        let approval=if args[9]=="--confirm" {
            use std::io::{IsTerminal,Write};
            if !std::io::stdin().is_terminal(){return Err("Interactive confirmation requires a terminal; no device accessed".into());}
            print!("\nTo authorize this write, type '{}': ",plan.confirmation_phrase());
            std::io::stdout().flush().map_err(|e|e.to_string())?;
            let mut line=String::new();
            std::io::stdin().read_line(&mut line).map_err(|e|e.to_string())?;
            if !plan.accepts_confirmation(&line){return Err("Cancelled; no device accessed".into());}
            plan.approval_token()
        }else{
            if args[9]!=plan.approval_token(){return Err("Exact approval token required, or use --confirm for an interactive prompt".into());}
            args[9].clone()
        };
        let link=midi::MidiLink::open(&args[6],&args[6])?;
        let mut device=transfer::Adapter{link,read_timeout:Duration::from_secs(3),stage_timeout:Duration::from_secs(3),commit_timeout:Duration::from_secs(30)};
        restore::execute(&mut device,&plan,&approval,true,true,|raw|{
            let timestamp=SystemTime::now().duration_since(UNIX_EPOCH).map_err(|e|e.to_string())?.as_secs();
            let m=song_manifest(raw,slot,&args[6],&args[6],timestamp)?;
            let mut file=OpenOptions::new().write(true).create_new(true).open(&args[5]).map_err(|e|e.to_string())?;
            write_song_archive(&mut file,raw,&m)?;file.sync_all().map_err(|e|e.to_string())?;drop(file);
            verify_song_archive(File::open(&args[5]).map_err(|e|e.to_string())?)?;
            eprintln!("Fresh slot{slot} recovery backup saved and verified before writes");Ok(())
        })?;
        println!("Slot{slot} replacement and full readback match succeeded. Reload slot{slot} from panel.");
        return Ok(());
    }
    if args.get(1).map(String::as_str)==Some("plan-restore") {
        if !(args.len()==6 || (args.len()==7 && args[6]=="--json")) {return Err("Usage: plan-restore SOURCE_SONG_ARCHIVE TARGET_BACKUP TARGET_SLOT replace|patch [--json] (offline only)".into());}
        let (_,after)=verify_song_archive(File::open(&args[2]).map_err(|e|e.to_string())?)?;
        let (base,before)=verify_song_archive(File::open(&args[3]).map_err(|e|e.to_string())?)?;
        let slot=args[4].parse::<usize>().map_err(|_|"Invalid target slot")?;
        if base["slot"]!=slot {return Err("Target backup belongs to a different slot".into());}
        let mode=match args[5].as_str(){"replace"=>restore::Mode::Replace,"patch"=>restore::Mode::Patch,_=>return Err("Choose replace or patch".into())};
        let plan=restore::Plan::new(slot,before,after,mode)?;
        if args.len()==7 {
            println!("{}",serde_json::to_string_pretty(&plan.summary()).map_err(|e|e.to_string())?);
        }else{
            println!("{}\n\nNo device accessed. Full diff: repeat with --json.\nTo write interactively, use restore-song with --confirm instead of a token.\n\nApproval token (only needed for noninteractive use):\n{}",plan.brief(),plan.approval_token());
        }
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
        return Err("Usage: carbon-backup ports | verify FILE | backup-bank FILE INPUT OUTPUT --stopped [--pause-ms N] | backup-song SLOT FILE INPUT OUTPUT --stopped [--pause-ms N]\nStop transport and avoid panel edits/save/load. See README for explicit restore and JSON commands.".into());
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
            // Backup path sends only read command0x70; restore is separately gated.
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
