// SPDX-License-Identifier: GPL-3.0-or-later
//! JSON parsing with duplicate object keys rejected at every nesting level.
use serde::de::{self,Deserialize,Deserializer,Visitor,MapAccess,SeqAccess};
use serde_json::{Value,Map,Number};
use std::fmt;
struct Strict(Value);
impl<'de> Deserialize<'de> for Strict {
 fn deserialize<D:Deserializer<'de>>(d:D)->Result<Self,D::Error>{
 struct V;
 impl<'de> Visitor<'de> for V {
 type Value=Strict;
 fn expecting(&self,f:&mut fmt::Formatter)->fmt::Result{write!(f,"JSON without duplicate keys")}
 fn visit_bool<E:de::Error>(self,v:bool)->Result<Strict,E>{Ok(Strict(Value::Bool(v)))}
 fn visit_i64<E:de::Error>(self,v:i64)->Result<Strict,E>{Ok(Strict(Value::Number(v.into())))}
 fn visit_u64<E:de::Error>(self,v:u64)->Result<Strict,E>{Ok(Strict(Value::Number(v.into())))}
 fn visit_f64<E:de::Error>(self,v:f64)->Result<Strict,E>{Number::from_f64(v).map(|n|Strict(Value::Number(n))).ok_or_else(||E::custom("Nonfinite number"))}
 fn visit_str<E:de::Error>(self,v:&str)->Result<Strict,E>{Ok(Strict(Value::String(v.into())))}
 fn visit_string<E:de::Error>(self,v:String)->Result<Strict,E>{Ok(Strict(Value::String(v)))}
 fn visit_unit<E:de::Error>(self)->Result<Strict,E>{Ok(Strict(Value::Null))}
 fn visit_seq<A:SeqAccess<'de>>(self,mut a:A)->Result<Strict,A::Error>{let mut v=vec![];while let Some(Strict(x))=a.next_element()?{v.push(x);}Ok(Strict(Value::Array(v)))}
 fn visit_map<A:MapAccess<'de>>(self,mut a:A)->Result<Strict,A::Error>{let mut m=Map::new();while let Some(k)=a.next_key::<String>()?{if m.contains_key(&k){return Err(de::Error::custom(format!("Duplicate JSON key: {k}")));}let Strict(v)=a.next_value()?;m.insert(k,v);}Ok(Strict(Value::Object(m)))}
 }
 d.deserialize_any(V)
 }
}
pub fn parse(text:&str)->Result<Value,String>{let mut d=serde_json::Deserializer::from_str(text);let Strict(v)=Strict::deserialize(&mut d).map_err(|e|e.to_string())?;d.end().map_err(|e|e.to_string())?;Ok(v)}
#[cfg(test)]mod tests{use super::*;
 #[test]fn duplicates(){for s in [r#"{"a":1,"a":2}"#,r#"{"outer":[{"a":1,"\u0061":2}]}"#]{assert!(parse(s).unwrap_err().contains("Duplicate"));}}
 #[test]fn values(){let s=r#"{"a":[null,true,false,-1,2,1.5,"text",{}]}"#;assert_eq!(parse(s).unwrap(),serde_json::from_str::<Value>(s).unwrap());assert!(parse("{} {}").is_err());}
}
