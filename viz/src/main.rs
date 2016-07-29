extern crate vcd;

use std::io;
use std::io::prelude::*;
use std::fs::File;

fn main() {
    ok().expect("Error");
}

fn ok() -> io::Result<()> {
    let mut f = try!(File::open("tb.vcd"));

    let mut reader = vcd::Parser::new(&mut f);

    let header = reader.parse_header().unwrap();
    println!("header {:?}", header);

    for cmd in reader {
        println!("line {:?}", cmd);
        //writer.command(&cmd.unwrap()).unwrap();
    }

    Ok(())
}
