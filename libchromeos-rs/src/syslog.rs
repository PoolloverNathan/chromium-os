// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::env;

use crosvm_base::unix::getpid;
use log::LevelFilter;
use log::SetLoggerError;
use stderrlog::StdErrLog;
use syslog::{BasicLogger, Facility, Formatter3164};

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("unix socket syslog setup failed: {0}")]
    SyslogUnix(#[source] syslog::Error),
    #[error("failed to set logger: {0}")]
    SetLoggerError(#[source] SetLoggerError),
}

pub type Result<T> = std::result::Result<T, Error>;

/// Get an identifier to be used with init(...) from the current process.
pub fn get_ident_from_process() -> Option<String> {
    env::current_exe()
        .map(|p| p.file_name().map(|n| n.to_string_lossy().to_string()))
        .unwrap_or(None)
}

/// Get the default syslog-based logger.
pub fn get_syslog_logger(ident: String) -> Result<BasicLogger> {
    Ok(BasicLogger::new(
        syslog::unix(Formatter3164 {
            facility: Facility::LOG_USER,
            hostname: None,
            pid: getpid() as u32,
            process: ident,
        })
        .map_err(Error::SyslogUnix)?,
    ))
}

/// Initialize logging to the system log and stderr if |log_to_stderr| is true.
///
/// Before logging is initialized eprintln(...) and println(...) should be
/// used. Afterward, debug!(...), info!(...), warn!(....), and error!(...)
/// should be used instead.
pub fn init(ident: String, log_to_stderr: bool) -> Result<()> {
    let syslog_logger = Box::new(get_syslog_logger(ident)?);

    if log_to_stderr {
        let mut stderr_logger = StdErrLog::new();
        stderr_logger.verbosity(5);

        multi_log::MultiLogger::init(
            vec![Box::new(stderr_logger), syslog_logger],
            log::Level::Info,
        )
    } else {
        log::set_boxed_logger(syslog_logger).map(|()| log::set_max_level(LevelFilter::Info))
    }
    .map_err(Error::SetLoggerError)
}
