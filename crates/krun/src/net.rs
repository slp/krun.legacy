use std::{
    fmt,
    os::{
        fd::{AsRawFd, IntoRawFd, OwnedFd},
        unix::net::UnixStream,
    },
    path::Path,
    process::Command,
};

use anyhow::{anyhow, Context, Result};
use rustix::net::{socketpair, AddressFamily, SocketFlags, SocketType};
use utils::{env::find_in_path, tracing::debug};

#[derive(Copy, Clone, Eq, PartialEq, Debug)]
pub enum NetMode {
    PASST = 0,
    TSI,
}

#[cfg_attr(feature = "tracing", tracing::instrument(level = "debug"))]
pub fn connect_to_passt<P>(passt_socket_path: P) -> Result<UnixStream>
where
    P: AsRef<Path> + fmt::Debug,
{
    Ok(UnixStream::connect(passt_socket_path)?)
}

#[cfg_attr(feature = "tracing", tracing::instrument(level = "debug"))]
pub fn start_passt() -> Result<OwnedFd> {
    let (parent_fd, child_fd) = socketpair(
        AddressFamily::UNIX,
        SocketType::STREAM,
        SocketFlags::empty(),
        None,
    )?;

    let passt_path = find_in_path("passt").context("Failed to look up `passt` in PATH")?;
    let passt_path = passt_path.ok_or_else(|| anyhow!("`passt` executable not found in PATH"))?;

    debug!(fd = child_fd.as_raw_fd(), "passing fd to passt");

    // SAFETY: `child_fd` is an `OwnedFd` and consumed to prevent closing on drop.
    // See https://doc.rust-lang.org/std/io/index.html#io-safety
    let child = Command::new(passt_path)
        .args(["-q", "-f", "--fd"])
        .arg(format!("{}", child_fd.into_raw_fd()))
        .spawn();
    if let Err(err) = child {
        return Err(err).context("Failed to execute `passt` as child process");
    }

    Ok(parent_fd)
}
