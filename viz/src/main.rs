extern crate vcd;
extern crate rustc_serialize;

use rustc_serialize::json;
use std::io;
use std::io::prelude::*;
use std::fs::File;
use std::collections::HashMap;

use vcd::{ScopeItem, IdCode, Command, Value};

fn main() {
    ok().expect("Error");
}

#[derive(Debug, Clone, RustcDecodable, RustcEncodable)]
struct Signal {
    name: String,
    points: Vec<(u64, u8)>,
}

fn ok() -> io::Result<()> {
    let mut f = try!(File::open("tb.vcd"));

    let mut reader = vcd::Parser::new(&mut f);

    let mut vars = HashMap::new();

    let header = reader.parse_header().unwrap();
    for item in &header.scope.children {
        match item {
            &ScopeItem::Var(ref item) => {
                vars.insert(item.code, Signal {
                    name: item.reference.to_string(),
                    points: vec![],
                });
                //println!("var {:?}", item);
            },
            _ => {}
        }
    }

    let mut timestamp = 0;
    for cmd in reader {
        match cmd {
            Ok(Command::Timestamp(new_time)) => {
                timestamp = new_time;
            }
            Ok(Command::ChangeScalar(id, value)) => {
                if let Some(ref mut var) = vars.get_mut(&id) {
                    var.points.push((timestamp, match value {
                        Value::V1 => 1,
                        Value::V0 | _ => 0,
                    }))
                }
            }
            what=> {
                println!("line {:?}", what);
            }
        }
        //writer.command(&cmd.unwrap()).unwrap();
    }

    for item in vars.values_mut() {
        let last = if let Some(last) = item.points.last().clone() {
            Some(last.clone())
        } else {
            None
        };
        if let Some(last) = last {
            item.points.push((timestamp + 20, last.1));
        }
    }

    let signals: Vec<Signal> = vars.into_iter().map(|(_, v)| v).collect();

    println!("var signals = {};", json::encode(&signals).unwrap());
    println!("var pointWidth = {};", timestamp + 20);

    Ok(())
}
