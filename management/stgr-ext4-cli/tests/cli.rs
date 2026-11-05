// SPDX-License-Identifier: MIT
//! Process-level tests for the management CLI.

use std::process::Command;

fn binary() -> Command {
    Command::new(env!("CARGO_BIN_EXE_stgr-ext4"))
}

#[test]
fn version_exits_zero_and_prints_the_version() {
    let output = binary().arg("version").output().expect("run stgr-ext4");

    assert!(output.status.success());
    assert!(String::from_utf8_lossy(&output.stdout).contains(env!("CARGO_PKG_VERSION")));
}

#[test]
fn mount_is_not_a_fake_success() {
    let output = binary().arg("mount").output().expect("run stgr-ext4");

    assert_eq!(output.status.code(), Some(70));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("not implemented"));
    assert!(stderr.contains("does not report a fake"));
}

#[test]
fn unknown_command_exits_with_usage_code() {
    let output = binary()
        .arg("this-command-does-not-exist")
        .output()
        .expect("run stgr-ext4");

    assert_eq!(output.status.code(), Some(64));
}
