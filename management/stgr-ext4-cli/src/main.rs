// SPDX-License-Identifier: MIT
//! STGR EXT4 management CLI.
//!
//! This is the Milestone 1 scaffold. It reports the truth:
//!
//! * `version` and `help` work.
//! * `status` queries the Windows service control manager for the driver
//!   service and the (not yet implemented) management service.
//! * Every other command is explicitly not implemented and exits with code
//!   70. No command returns a fake success.

use std::env;
use std::process::{self, Command};

const VERSION: &str = env!("CARGO_PKG_VERSION");

const EXIT_OK: i32 = 0;
const EXIT_USAGE: i32 = 64;
const EXIT_UNAVAILABLE: i32 = 69;
const EXIT_NOT_IMPLEMENTED: i32 = 70;

const DRIVER_SERVICE: &str = "STGRExt4";
const MANAGEMENT_SERVICE: &str = "STGRExt4Service";

fn main() {
    let args: Vec<String> = env::args().skip(1).collect();
    process::exit(run(&args));
}

fn run(args: &[String]) -> i32 {
    match args.first().map(String::as_str) {
        None | Some("help") | Some("--help") | Some("-h") => {
            print_help();
            EXIT_OK
        }
        Some("version") | Some("--version") | Some("-V") => {
            println!("stgr-ext4 {VERSION}");
            EXIT_OK
        }
        Some("status") => status(),
        Some(command @ ("scan" | "list" | "mount" | "unmount" | "configure" | "diagnostics")) => {
            not_implemented(command)
        }
        Some(other) => {
            eprintln!("error: unknown command '{other}'");
            eprintln!();
            print_help();
            EXIT_USAGE
        }
    }
}

fn print_help() {
    print!(
        r#"stgr-ext4 {VERSION}

Usage: stgr-ext4 <command>

Commands:
  version       Print the management CLI version
  status        Query the STGR EXT4 driver service state
  scan          Discover EXT4 volumes (not implemented)
  list          List configured volumes (not implemented)
  mount         Mount an approved volume (not implemented)
  unmount       Dismount a volume (not implemented)
  configure     Edit the approved-volume configuration (not implemented)
  diagnostics   Collect diagnostics (not implemented)
  help          Print this help

Exit codes:
  0   success
  64  usage error
  69  unavailable (wrong operating system or missing tool)
  70  command not implemented in this build

Only explicitly approved EXT4 volumes may ever be mounted. See
docs/CONFIGURATION.md and docs/DATA-INTEGRITY.md.
"#
    );
}

fn status() -> i32 {
    if !cfg!(windows) {
        eprintln!("error: 'status' requires Windows");
        return EXIT_UNAVAILABLE;
    }

    println!("STGR EXT4 status");
    println!("  build: {VERSION}");

    match query_service(DRIVER_SERVICE) {
        Ok(text) => {
            println!("  driver service ({DRIVER_SERVICE}): present");
            print_state_line(&text);
        }
        Err(message) => report_query_failure(DRIVER_SERVICE, &message),
    }

    match query_service(MANAGEMENT_SERVICE) {
        Ok(text) => {
            println!("  management service ({MANAGEMENT_SERVICE}): present");
            print_state_line(&text);
        }
        Err(message) => report_query_failure(MANAGEMENT_SERVICE, &message),
    }

    println!("  filesystem mounting: not implemented in {VERSION}");
    println!("  driver control interface: not implemented in {VERSION}");

    EXIT_OK
}

fn query_service(name: &str) -> Result<String, String> {
    let output = Command::new("sc.exe")
        .args(["query", name])
        .output()
        .map_err(|error| format!("could not run sc.exe: {error}"))?;

    let mut text = String::from_utf8_lossy(&output.stdout).into_owned();

    if !output.status.success() {
        text.push_str(&String::from_utf8_lossy(&output.stderr));
        return Err(text.trim().to_owned());
    }

    Ok(text)
}

fn print_state_line(text: &str) {
    for line in text.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with("STATE") {
            println!("    {trimmed}");
        }
    }
}

fn report_query_failure(name: &str, message: &str) {
    if message.contains("1060") {
        println!("  service ({name}): not installed (Windows error 1060)");
    } else if message.is_empty() {
        println!("  service ({name}): query failed with no diagnostic text");
    } else {
        println!("  service ({name}): query failed: {message}");
    }
}

fn not_implemented(command: &str) -> i32 {
    eprintln!("error: '{command}' is not implemented in STGR EXT4 {VERSION}");
    eprintln!();
    eprintln!("The native driver is at Milestone 1: it registers with the Windows");
    eprintln!("I/O manager and safely refuses every volume. No filesystem is mounted");
    eprintln!("and no write support exists. This command does not report a fake");
    eprintln!("success. See ROADMAP.md and docs/FEASIBILITY.md.");
    EXIT_NOT_IMPLEMENTED
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn version_command_succeeds() {
        assert_eq!(run(&["version".to_owned()]), EXIT_OK);
    }

    #[test]
    fn help_command_succeeds() {
        assert_eq!(run(&["help".to_owned()]), EXIT_OK);
    }

    #[test]
    fn unknown_command_is_a_usage_error() {
        assert_eq!(run(&["definitely-not-a-command".to_owned()]), EXIT_USAGE);
    }

    #[test]
    fn unimplemented_commands_never_report_success() {
        for command in [
            "scan",
            "list",
            "mount",
            "unmount",
            "configure",
            "diagnostics",
        ] {
            assert_eq!(
                run(&[command.to_owned()]),
                EXIT_NOT_IMPLEMENTED,
                "command {command} must not return success"
            );
        }
    }
}
