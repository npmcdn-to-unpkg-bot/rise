extern crate vcd;
extern crate rustc_serialize;
extern crate iron;
extern crate staticfile;
extern crate mount;
extern crate router;


use std::path::Path;

use mount::Mount;
use std::env;

use rustc_serialize::json;
use std::io;
use std::io::prelude::*;
use std::fs::File;
use std::collections::HashMap;
//use iron::{Iron, Request, Response, IronResult, Handler};
use iron::prelude::*;
use iron::headers::ContentType;
use iron::modifiers::Header;
use iron::status;
use router::Router;

use vcd::{ScopeItem, IdCode, Command, Value};

fn serve() {
    let mut mount = Mount::new();

    fn root(_: &mut Request) -> IronResult<Response> {
        Ok(Response::with((status::Ok, Header(ContentType::html()), &include_bytes!("index.html")[..])))
    }

    let mut router = Router::new();  // Alternative syntax:
    router.get("/", root);
    router.get("/vcd", handler_vcd);

    // Serve the shared JS/CSS at /
    mount.mount("/", router);

    println!("Server running on http://localhost:3000/");

    Iron::new(mount).http("127.0.0.1:3000").unwrap();
}


fn main() {
    //ok().expect("Error");
    serve();
}

#[derive(Debug, Clone, RustcDecodable, RustcEncodable)]
struct Signal {
    name: String,
    points: Vec<(u64, u8)>,
}

#[derive(Debug, Clone, RustcDecodable, RustcEncodable)]
struct VcdDump {
    duration: u64,
    signals: Vec<Signal>,
}

fn handler_vcd(_: &mut Request) -> IronResult<Response> {
    let mut f = File::open(env::args().nth(1).unwrap()).unwrap();

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

    let mut signals: Vec<Signal> = vars.into_iter()
        .map(|(_, v)| v)
        .collect();

    signals.sort_by_key(|x| x.name.to_owned());

    let res = json::encode(&VcdDump {
        duration: timestamp + 20,
        signals: signals,
    }).unwrap();

    Ok(Response::with((status::Ok, Header(ContentType::json()), res.as_bytes())))
}
