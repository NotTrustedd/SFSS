# Secure File Sharing and Access Control

This is the **single final project**: a simple Windows desktop app for sharing
files directly between two computers over a local network. It uses C++17,
Winsock TCP, local accounts, file handling, a session-key check, and transfer
history.

## Requirements

- Windows 10/11
- MinGW g++ with C++17 support
- `mingw32-make`

The GUI uses native Win32 controls because Qt is not installed in the current
MinGW environment. This application is Windows-only; it is not a
cross-platform Qt build.

## Build and run

From PowerShell in this project folder:

```powershell
mingw32-make clean
mingw32-make
.\SecureShare.exe
```

The app executable is statically linked with the MinGW C++ runtime, so the
other computer does not need MinGW installed. Keep the executable in a folder
where the user can create files; the app stores its data beside the executable.

Build and run the integration test:

```powershell
mingw32-make test
```

## Using two computers

1. Copy `SecureShare.exe` to a writable folder on each Windows 10/11 computer.
   The computers should be on the same LAN. No MinGW install is needed to run
   this statically linked executable.
2. Start the app on both computers. Register a separate local account on each,
   then log in. Accounts are local to each computer; account registration does
   not synchronize over the network.
3. Use a listening port on each computer (the default `54000` can be used on
   both computers). Allow the app through Windows Firewall on a **Private**
   network, or allow inbound TCP traffic on that port.
4. On the receiving computer, share its LAN IPv4 address and listening port.
   On the sending computer, enter that address, port, and the receiver's
   username.
5. Enter the same shared session key on both computers. Choose a file and
   send it. On the receiving computer, select **Incoming requests**, verify
   the request, then accept or reject it.

Find the LAN IPv4 address with `ipconfig` in Command Prompt. Guest Wi-Fi,
client isolation, different VLANs, or firewall rules may block peer traffic.
This app is designed for a trusted LAN demo, not Internet file sharing.

The app creates `data\` and `received\` beside `SecureShare.exe`. Accounts and
history are local; received files are saved in `received\`. Each computer
maintains its own account and history files.

## Included features

- Registration, login, and logout
- A listener per online peer, manual peer status checks
- Direct TCP file transfer and incoming request accept/reject
- Shared-key proof validation before acceptance
- Transfer progress/cancellation, file selection, and collision-safe saving
- Chronological history and search by username
- Error/status reporting, with account, transfer, and history sections in the
  GUI

The integration test creates temporary accounts and a binary payload, checks
login and invalid-password behavior, validates an invalid key, sends and
verifies a 180,000-byte file between local peers over TCP, exercises rejection,
and verifies transfer-history records.

## Architecture

- `include/` and `src/`: authentication, network protocol/server/client,
  security/file handling, transfer orchestration, and the GUI entry point
- `tests/integration.cpp`: local end-to-end transfer test
- `data/`: runtime account and history files
- `received/`: files received by this peer

## Security and scope limitations

This is a LAN college demonstration, not production-secure software. The
file cipher is XOR and is **not secure encryption**. Password hashing is
demonstration-grade, and peers exchange their IP/port and shared key out of
band. Do not use this app for sensitive files or over an untrusted network.
