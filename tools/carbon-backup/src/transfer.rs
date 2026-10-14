// SPDX-License-Identifier: GPL-3.0-or-later
//! Deadline-bounded adapter; hardware write wiring deliberately absent.
use std::time::{Duration,Instant};
use crate::{request,response,song_range,restore::{ack,Device}};
pub trait Link {fn send(&mut self,p:&[u8])->Result<(),String>;
    /// MUST return by deadline; None is timeout, never retry automatically.
    fn receive_until(&mut self,deadline:Instant)->Result<Option<Vec<u8>>,String>;}
pub struct Adapter<L:Link>{pub link:L,pub read_timeout:Duration,pub stage_timeout:Duration,pub commit_timeout:Duration}
impl<L:Link> Adapter<L>{
    fn receive(&mut self,deadline:Instant)->Result<Vec<u8>,String>{
        if Instant::now()>=deadline{return Err("Timeout: abort without retry".into());}
        let result=self.link.receive_until(deadline)?.ok_or("Timeout: abort without retry")?;
        if Instant::now()>deadline{return Err("Late response: abort without retry".into());}Ok(result)
    }
}
impl<L:Link> Device for Adapter<L>{
 fn read_song(&mut self,slot:usize)->Result<Vec<u8>,String>{
    let mut data=Vec::new();
    for address in song_range(slot)?.step_by(64){
        self.link.send(&request(address)?)?;let deadline=Instant::now()+self.read_timeout;
        loop {let p=self.receive(deadline)?;if let Some(bytes)=response(&p,address)?{data.extend(bytes);break;}}
    }Ok(data)
 }
 fn exchange(&mut self,p:&[u8])->Result<Vec<u8>,String>{
    if p.len()<7 || !p.starts_with(&[0xf0,0,1,0x72,0x49]) || p.last()!=Some(&0xf7){return Err("Bad outbound write framing".into());}
    let timeout=match p[5]{0x72=>self.stage_timeout,0x73=>self.commit_timeout,_=>return Err("Only stage/commit accepted".into())};
    self.link.send(p)?;let deadline=Instant::now()+timeout;
    loop {
        let reply=self.receive(deadline)?;
        if reply.starts_with(&[0xf0,0,1,0x72,0x49]) {ack(&reply,p[5])?;return Ok(reply);}
    }
 }
}
#[cfg(test)] mod tests{
 use super::*;use crate::restore::{stage,commit};
 struct Fake{sent:Vec<Vec<u8>>,reply:Option<Vec<u8>>}
 impl Link for Fake{fn send(&mut self,p:&[u8])->Result<(),String>{self.sent.push(p.to_vec());Ok(())}
 fn receive_until(&mut self,_:Instant)->Result<Option<Vec<u8>>,String>{Ok(self.reply.take())}}
 fn adapter(reply:Option<Vec<u8>>)->Adapter<Fake>{Adapter{link:Fake{sent:vec![],reply},read_timeout:Duration::from_secs(3),stage_timeout:Duration::from_secs(3),commit_timeout:Duration::from_secs(30)}}
 #[test] fn timeout_no_retry(){let mut a=adapter(None);assert!(a.exchange(&commit(0).unwrap()).is_err());assert_eq!(a.link.sent.len(),1);}
 #[test] fn ack_matching(){let mut a=adapter(Some(vec![0xf0,0,1,0x72,0x49,1,0x72,1,0xf7]));assert!(a.exchange(&stage(0,&[1]).unwrap()).is_ok());let mut a=adapter(Some(vec![0xf0,0,1,0x72,0x49,1,0x72,1,0xf7]));assert!(a.exchange(&commit(0).unwrap()).is_err());}
}
