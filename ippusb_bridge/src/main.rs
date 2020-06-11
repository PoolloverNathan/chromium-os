// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use std::os::unix::io::IntoRawFd;
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};

use sys_util::{register_signal_handler, EventFd};

mod arguments;
use arguments::Args;

#[derive(Debug)]
pub enum Error {
    EventFd(sys_util::Error),
    ParseArgs(arguments::Error),
    RegisterHandler(sys_util::Error),
    SysUtil(sys_util::Error),
}

impl std::error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            EventFd(err) => write!(f, "Failed to create/duplicate EventFd: {}", err),
            ParseArgs(err) => write!(f, "Failed to parse arguments: {}", err),
            RegisterHandler(err) => write!(f, "Registering SIGINT handler failed: {}", err),
            SysUtil(err) => write!(f, "Sysutil error: {}", err),
        }
    }
}

type Result<T> = std::result::Result<T, Error>;

// Set to true if the program should terminate.
static SHUTDOWN: AtomicBool = AtomicBool::new(false);

// Holds a raw EventFD with 'static lifetime that can be used to wake up any
// polling threads.
static SHUTDOWN_FD: AtomicI32 = AtomicI32::new(-1);

extern "C" fn sigint_handler() {
    // Check if we've already received one SIGINT. If we have, the program may be misbehaving and
    // not terminating, so to be safe we'll forcefully exit.
    if SHUTDOWN.load(Ordering::Relaxed) {
        std::process::exit(1);
    }
    SHUTDOWN.store(true, Ordering::Relaxed);
    let fd = SHUTDOWN_FD.load(Ordering::Relaxed);
    if fd >= 0 {
        let buf = &1u64 as *const u64 as *const libc::c_void;
        let size = std::mem::size_of::<u64>();
        unsafe { libc::write(fd, buf, size) };
    }
}

/// Registers a SIGINT handler that, when triggered, will write to `shutdown_fd`
/// to notify any listeners of a pending shutdown.
fn add_sigint_handler(shutdown_fd: EventFd) -> sys_util::Result<()> {
    // Leak our copy of the fd to ensure SHUTDOWN_FD remains valid until ippusb_bridge closes, so
    // that we aren't inadvertently writing to an invalid FD in the SIGINT handler. The FD will be
    // reclaimed by the OS once our process has stopped.
    SHUTDOWN_FD.store(shutdown_fd.into_raw_fd(), Ordering::Relaxed);

    const SIGINT: libc::c_int = 2;
    // Safe because sigint_handler is an extern "C" function that only performs
    // async signal-safe operations.
    unsafe { register_signal_handler(SIGINT, sigint_handler) }
}

fn run() -> Result<()> {
    let argv: Vec<String> = std::env::args().collect();
    let _args = match Args::parse(&argv).map_err(Error::ParseArgs)? {
        None => return Ok(()),
        Some(args) => args,
    };

    let shutdown_fd = EventFd::new().map_err(Error::EventFd)?;
    add_sigint_handler(shutdown_fd).map_err(Error::RegisterHandler)?;
    Ok(())
}

fn main() {
    // Use run() instead of returning a Result from main() so that we can print
    // errors using Display instead of Debug.
    if let Err(e) = run() {
        eprintln!("{}", e);
    }
}
