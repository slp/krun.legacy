use std::{fs, os::unix::process::ExitStatusExt, process::Command};

use anyhow::{anyhow, Context, Result};
use rustix::system::sethostname;
use utils::tracing::debug;

#[cfg_attr(feature = "tracing", tracing::instrument(level = "debug"))]
pub fn configure_network() -> Result<()> {
    {
        let hostname =
            fs::read_to_string("/etc/hostname").context("Failed to read `/etc/hostname`")?;
        let hostname = if let Some((hostname, _)) = hostname.split_once('\n') {
            hostname.to_owned()
        } else {
            hostname
        };
        sethostname(hostname.as_bytes()).context("Failed to set hostname")?;
    }

    let output = Command::new("/sbin/dhclient")
        .output()
        .context("Failed to execute `dhclient` as child process")?;
    debug!(?output, "dhclient output");
    if !output.status.success() {
        let err = if let Some(code) = output.status.code() {
            anyhow!("`dhclient` process exited with status code: {code}")
        } else {
            anyhow!(
                "`dhclient` process terminated by signal: {}",
                output
                    .status
                    .signal()
                    .expect("either one of status code or signal should be set")
            )
        };
        Err(err)?;
    }

    Ok(())
}
