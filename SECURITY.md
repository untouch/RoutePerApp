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
`C:\ProgramData\RoutePerApp` - the rules, the resolved rules and both helper DLLs. This is the
weakest part of the design, and it is worth understanding:

- The interface replaces the helper copies from the folder of `routeperapp.exe` when they differ,
  so a swapped file does not stay for long.
- The folder is nevertheless writable by ordinary users today, and the elevated program injects
  from it. A standard user who can write there can therefore get their own DLL loaded into
  processes belonging to other users. That is a local privilege escalation path, and closing it is
  the next planned change: restrict write access when the folder is created, and compare the
  helper file against the copy next to the executable before injecting.
- Until then: do not put the files in a folder that other users can write, and do not leave
  non-administrator accounts on a machine where elevated RoutePerApp runs.

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
