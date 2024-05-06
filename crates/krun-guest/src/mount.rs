use std::{fs::OpenOptions, os::fd::AsFd};

use anyhow::{Context, Result};
use rustix::{
    fs::CWD,
    mount::{mount2, move_mount, open_tree, MountFlags, MoveMountFlags, OpenTreeFlags},
};

#[cfg_attr(feature = "tracing", tracing::instrument(level = "debug"))]
pub fn mount_filesystems() -> Result<()> {
    mount2(
        Some("tmpfs"),
        "/var/run",
        Some("tmpfs"),
        MountFlags::NOEXEC | MountFlags::NOSUID | MountFlags::RELATIME,
        None,
    )
    .context("Failed to mount `/var/run`")?;

    let _ = OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open("/tmp/resolv.conf")
        .context("Failed to create `/tmp/resolv.conf`")?;

    {
        let fd = open_tree(
            CWD,
            "/tmp/resolv.conf",
            OpenTreeFlags::OPEN_TREE_CLONE | OpenTreeFlags::OPEN_TREE_CLOEXEC,
        )
        .context("Failed to open_tree `/tmp/resolv.conf`")?;

        move_mount(
            fd.as_fd(),
            "",
            CWD,
            "/etc/resolv.conf",
            MoveMountFlags::MOVE_MOUNT_F_EMPTY_PATH,
        )
        .context("Failed to move_mount `/etc/resolv.conf`")?;
    }

    mount2(
        Some("binfmt_misc"),
        "/proc/sys/fs/binfmt_misc",
        Some("binfmt_misc"),
        MountFlags::NOEXEC | MountFlags::NOSUID | MountFlags::RELATIME,
        None,
    )
    .context("Failed to mount `binfmt_misc`")?;

    Ok(())
}
