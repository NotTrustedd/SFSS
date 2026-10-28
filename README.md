# Secure File Sharing and Access Control

This is the **single final project**: a native Windows desktop GUI backed by
C++17 authentication, Winsock peer networking, file handling, demonstration
encryption, and transfer-history modules.

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

Build and run the integration test:

```powershell
mingw32-make test
```

## Using two computers

1. Copy the complete project folder to each Windows computer and build it
   there, or copy the generated executable if its MinGW runtime dependencies
   are available.
2. Register a local account on each computer and log in using a distinct
   listening TCP port.
3. Allow the chosen listening port through Windows Firewall.
4. Exchange the receiver's LAN IPv4 address and port manually. The network
   must allow peer-to-peer connections; guest Wi-Fi/client isolation can block
   them.
5. Select the file, enter the peer information, and use the same shared key
   at sender and receiver.

Accounts and transfer history are stored under `data\`; downloaded files are
written under `received\`. These paths are relative to the app's working
directory.

## Included features

- Registration, login, and logout
- A listener per online peer, manual peer status checks
- Direct TCP file transfer and incoming request accept/reject
- Shared-key proof validation before acceptance
- Transfer progress/cancellation, file selection, and collision-safe saving
- Chronological history and search by username
- Error/status reporting

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
current file cipher is XOR, the password hashing scheme is not a modern
password KDF, and peers exchange IP/port and the shared key out-of-band. Use
AES-GCM, a standard password KDF, and an authenticated peer/key-management
design before handling sensitive files.
