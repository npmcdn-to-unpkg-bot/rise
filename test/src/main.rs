use std::net::UdpSocket;
use std::thread;
use std::time::Duration;
use std::io;
use std::ffi::CString;

fn main() {
    let _ = inner();
}

const char_a: u8 = b'%';
const char_b: u8 = b'$';

fn inner() -> io::Result<()> {

    let a = thread::spawn::<_, io::Result<()>>(|| {
        let mut socket = try!(UdpSocket::bind("0.0.0.0:0"));
        let mut buf = [0; 256];
        buf[0] = char_a;
        buf[1] = char_b;
        loop {
            try!(socket.send_to(&buf, "169.254.0.144:4111"));
            thread::sleep(Duration::from_secs(1));
        }

        Ok(())
    });

    let b = thread::spawn::<_, io::Result<()>>(|| {
        let mut socket = try!(UdpSocket::bind("0.0.0.0:3333"));
        loop {
            let mut buf = [0; 256];
            let (amt, src) = try!(socket.recv_from(&mut buf));
            if buf[0] == char_a && buf[1] == char_b {
                println!("sync: {:?}", CString::new(buf.iter().filter(|&x| *x != 0).cloned().collect::<Vec<u8>>()));
            }
        }

        Ok(())
    });

    a.join();
    b.join();

    Ok(())
}