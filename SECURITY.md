# Security

Read this before reporting something as malware, and before trusting a binary you did not build.

## What the program does that anti-virus products react to

- It loads a DLL into other processes (`CreateRemoteThread` plus `LoadLibrary`), 32-bit and 64-bit
  targets, with two helper files `hook.dll` and `hook32.dll`.
- Inside those processes it intercepts the Winsock calls that pick a source address, and rebinds
  sockets to the adapter you chose.
- It needs administrator rights, and it keeps a window hidden in the notification area.
- It reads the process list and the TCP connection table continuously.

That is the same behaviour pattern as a launcher for a cheat or a trojan, so behaviour-based
detectors can report it - detections with names like `PDM:Trojan.Win32.Generic` come from the
activity monitor, not from a signature. Nothing is hidden: the whole program is in this
repository, `build.cmd` is the only build step, and nothing is downloaded while building. If your
product objects to the prebuilt file, build the source yourself and compare.

## Why administrator rights are needed

Only to reach processes that belong to other users or to services. Without them the interface
still works: rules can be matched and saved, but the binding is not applied, and the status line
says so and offers a restart as administrator. There is no hidden elevation step, the requirement
is in the manifest (`requireAdministrator`) and visible in the file properties.

## Trust boundary of the shared folder

The engine, the injected helper and the interface exchange state through plain files in
`C:\ProgramData\RoutePerApp`: the rules, the resolved rules, and the helper DLLs in the
subfolder `hooks`.

The helper DLLs are the dangerous files, because an elevated RoutePerApp loads them into processes
that belong to other users. They are protected in three steps:

- The only trusted copy is the one next to `routeperapp.exe`, and that folder is written by an
  administrator when the program is installed. Nothing is injected from anywhere else.
- RoutePerApp copies both DLLs into `C:\ProgramData\RoutePerApp\hooks` and compares the SHA-256
  of the copy with the original. Injection is refused when a file is missing or the hashes
  differ, and the status line plus the log name the file.
- On that same step the `hooks` folder gets its own access list: system and administrators may
  write, ordinary users may only read and execute. Inherited write access from the parent folder
  is dropped, for the folder and for the files inside it. Helper files that lay directly in the
  data folder (up to version 1.1) are deleted, because any local account could have replaced them.

Check it yourself: `icacls C:\ProgramData\RoutePerApp\hooks` must show no write permission for
ordinary users. When RoutePerApp cannot set that list, it says so in the status line instead of
pretending the folder is safe.

Still open: the rule files in the data folder stay writable by ordinary users, since the program
writes them while running elevated. Writing rules can only move traffic between adapters on that
machine - it cannot load code. Do not share the folder with accounts you do not trust anyway.

## What the program is not

It is not a firewall, not a sandbox and not a permission system. It cannot stop a program from
reaching the network over another route, it cannot enforce a rule for traffic that was already
open when the assignment changed, and it cannot route name resolution: the Windows DNS Client
service resolves names over the default route, so a rule is only as good as the connections that
follow the answer.

Programs with anti-cheat or anti-injection may refuse the helper DLL, and an injected DLL can be
mistaken for a cheat by such software. Using RoutePerApp on those programs is at your own risk and
can get an account flagged.

## Reporting a problem

Write to kevenhall@kevenhall.de, ideally with the version from the title bar or the About dialog,
the Windows version, and `selbsttest.log` from `C:\ProgramData\RoutePerApp` (run
`routeperapp.exe --selbsttest` as administrator first). For a problem that should not be public,
say so in the subject and I will answer privately before anything is published.
