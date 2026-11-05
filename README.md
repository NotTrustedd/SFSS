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
The application icon is embedded from `assets\secureshare.ico` during the
build. After changing that icon, rebuild the app and copy the new
`SecureShare.exe` to the other computer.

Build and run the integration test:

```powershell
mingw32-make test
```

## Run a transfer between two Windows PCs

The app communicates directly over TCP. Both PCs need to be on a network that
allows devices to connect to each other. This walkthrough assumes one PC is the
sender and the other is the receiver.

### 1. Copy and start the application

1. Copy `SecureShare.exe` to a writable folder on each PC, for example
   `C:\Users\YourName\Desktop\SecureShare\`. Do not put it in `Program Files`.
   The receiver's `received\` folder and each PC's `data\` folder are created
   beside the executable.
2. Start `SecureShare.exe` on both PCs. The second PC does not need MinGW
   installed.
3. If Windows Firewall asks, allow the app on **Private networks only**, and
   only when you trust the network.

### 2. Create local accounts and start listening

1. On each PC, choose a username and password, then click **Register**.
   Usernames may contain letters, numbers, `_` and `-`; passwords must be at
   least 8 characters.
2. Click **Login** on both PCs. Leave **Listen port** set to `54000`, unless
   that port is already in use. The listening port can be the same on two
   different PCs.
3. Confirm the app reports that each user is online and listening. Accounts
   are stored separately on each PC; registration does not synchronize them.

### 3. Find the receiver's network address

On the receiver PC:

1. Open Command Prompt (press Start, type `cmd`, and open **Command Prompt**).
2. Run `ipconfig`.
3. Under the active **Wi-Fi** or **Ethernet** adapter, note the **IPv4
   Address**, such as `192.168.1.25`.

Use the receiver's current LAN IPv4 address—not `127.0.0.1`. Do not use an
address from an inactive adapter or a VPN.

### 4. Allow the receiver through Windows Firewall

The receiver must accept inbound TCP connections on its listening port
(normally `54000`). In Windows Security, use **Firewall & network protection
→ Allow an app through firewall** to allow Secure Share on a trusted **Private**
network. If needed, create an inbound firewall rule for TCP port `54000` on
the trusted network profile.

Do not disable the firewall. Do not mark an untrusted public network as
Private just to make the transfer work. If the network is identified as
Public, a Private-only firewall rule will not apply; use only a policy
appropriate for a network you trust. Guest Wi-Fi, client isolation, VPNs, or
different network segments may prevent the PCs from connecting.

### 5. Send and accept a file

On the **sender PC**, while logged in:

1. In **Receiver username**, enter the account name registered on the receiver.
2. In **Peer IPv4 address**, enter the receiver's address from step 3.
3. In **Listening port**, enter the port shown by the receiver (normally
   `54000`).
4. Choose a **Shared session key** of at least 8 characters. Tell the receiver
   the exact key using a separate trusted method; it is case-sensitive and
   spaces count.
5. Click **Browse...**, select a small test file, then click **Send file**.

On the **receiver PC**, while logged in:

1. Enter exactly the same key in **Shared session key**.
2. Click **Incoming requests**. A request appears only after the sender has
   connected and sent it.
3. Check the sender, filename, and size in the prompt. Choose **Yes** to
   accept or **No** to reject.
4. Keep both apps open until the transfer finishes. The receiver should see a
   success message containing the saved-file path.

The file is saved in `received\` beside the receiver's `SecureShare.exe`. For
example, if the app is in
`C:\Users\YourName\Desktop\SecureShare\SecureShare.exe`, look in
`C:\Users\YourName\Desktop\SecureShare\received\`. Accounts and transfer
history are also local to each PC.

### Troubleshooting

- **No incoming request:** The receiver only displays requests that have
  already arrived. First confirm the sender's connection succeeds.
- **Winsock error 10060 / connection timeout:** Check that the receiver is
  logged in and listening, that the sender has the receiver's current IPv4
  address and exact listening port, and that inbound TCP is allowed by the
  receiver's firewall. Confirm the Wi-Fi allows devices to communicate.
- **`TcpTestSucceeded : False`:** From PowerShell on the sender, test the
  receiver's address and port:
  ```powershell
  Test-NetConnection 192.168.1.25 -Port 54000
  ```
  Replace `192.168.1.25` with the receiver's actual IPv4 address and use its
  actual listening port. A successful connection reports
  `TcpTestSucceeded : True`. If it is false, the app's request cannot reach the
  receiver yet.
- **Session key did not match:** The request reached the receiver, but the
  keys differ. Enter the exact same case-sensitive key on both PCs. The
  current request remains pending so the receiver can try again.
- **Port already in use:** Choose another unused port for that PC at login,
  add a firewall rule for that port, and enter that same port on the sender.

Start with a small file. The current maximum transfer size is **64 MiB**. This
application is for a trusted LAN demonstration, not Internet file sharing or
sensitive data.

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
