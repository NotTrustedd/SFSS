#include "auth/Authentication.h"
#include "network/Client.h"
#include "network/Protocol.h"
#include "network/Server.h"
#include "security/FileManager.h"
#include "transfer/HistoryManager.h"
#include "transfer/TransferManager.h"
#include <commdlg.h>
#include <memory>
#include <process.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

namespace {

const UINT WM_APP_OPERATION_DONE = WM_APP + 1;
const UINT WM_APP_TRANSFER_PROGRESS = WM_APP + 2;
const int ID_USERNAME = 1000;
const int ID_PASSWORD = 1001;
const int ID_PORT = 1002;
const int ID_LOGIN = 1003;
const int ID_REGISTER = 1004;
const int ID_RECEIVER = 1010;
const int ID_ADDRESS = 1011;
const int ID_PEER_PORT = 1012;
const int ID_FILE_PATH = 1013;
const int ID_BROWSE = 1014;
const int ID_SESSION_KEY = 1015;
const int ID_SEND = 1016;
const int ID_PENDING = 1017;
const int ID_HISTORY = 1018;
const int ID_LOGOUT = 1019;
const int ID_SEARCH = 1020;
const int ID_SEARCH_BUTTON = 1021;
const int ID_PEER_CHECK = 1022;
const int ID_CANCEL = 1023;

struct AppState {
    Authentication auth;
    HistoryManager history;
    Server server;
    std::unique_ptr<TransferManager> transfers;
    std::string currentUser;
    HWND hwnd;
    HWND username;
    HWND password;
    HWND listenPort;
    HWND receiver;
    HWND address;
    HWND peerPort;
    HWND filePath;
    HWND sessionKey;
    HWND searchUser;
    HWND status;
    HWND log;
    HWND loginButton;
    HWND registerButton;
    HWND sendButton;
    HWND pendingButton;
    HWND historyButton;
    HWND logoutButton;
    HWND browseButton;
    HWND searchButton;
    HWND peerCheckButton;
    HWND cancelButton;
    HANDLE worker;
    bool operationRunning;
    bool closing;
    volatile LONG cancelRequested;

    AppState()
        : auth("data\\accounts.txt"), history("data\\history.txt"), hwnd(NULL),
          username(NULL), password(NULL), listenPort(NULL), receiver(NULL),
          address(NULL), peerPort(NULL), filePath(NULL), sessionKey(NULL),
          searchUser(NULL), status(NULL), log(NULL), loginButton(NULL),
          registerButton(NULL), sendButton(NULL), pendingButton(NULL),
          historyButton(NULL), logoutButton(NULL), browseButton(NULL),
          searchButton(NULL), peerCheckButton(NULL), cancelButton(NULL),
          worker(NULL), operationRunning(false), closing(false),
          cancelRequested(0) {}
};

struct WorkerArgs {
    HWND hwnd;
    TransferManager* transfers;
    IncomingOffer offer;
    std::string sender;
    std::string receiver;
    std::string address;
    std::string path;
    std::string key;
    unsigned short port;
    bool incoming;
    volatile LONG* cancelRequested;
};

struct OperationResult {
    bool success;
    std::string message;
};

AppState* g_app = NULL;

std::string getText(HWND control) {
    const int length = GetWindowTextLengthA(control);
    if (length <= 0) {
        return std::string();
    }
    std::vector<char> buffer(static_cast<std::size_t>(length) + 1, '\0');
    GetWindowTextA(control, &buffer[0], static_cast<int>(buffer.size()));
    return std::string(&buffer[0]);
}

unsigned short parsePort(const std::string& input, unsigned short fallback) {
    if (input.empty()) {
        return fallback;
    }
    std::istringstream parser(input);
    unsigned long value = 0;
    parser >> value;
    if (!parser || !parser.eof() || value == 0 || value > 65535) {
        throw std::invalid_argument("Port must be a number from 1 to 65535.");
    }
    return static_cast<unsigned short>(value);
}

void appendLog(const std::string& message) {
    if (g_app == NULL || g_app->log == NULL) {
        return;
    }
    const int count = static_cast<int>(SendMessageA(g_app->log, LB_GETCOUNT, 0, 0));
    SendMessageA(g_app->log, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(message.c_str()));
    SendMessageA(g_app->log, LB_SETTOPINDEX, count, 0);
}

void showError(const char* title, const std::string& message) {
    MessageBoxA(g_app == NULL ? NULL : g_app->hwnd, message.c_str(), title,
                MB_OK | MB_ICONERROR);
}

void setStatus(const std::string& text) {
    if (g_app != NULL && g_app->status != NULL) {
        SetWindowTextA(g_app->status, text.c_str());
    }
}

void setOperationButtons(bool enabled) {
    EnableWindow(g_app->sendButton, enabled);
    EnableWindow(g_app->pendingButton, enabled);
    EnableWindow(g_app->historyButton, enabled);
    EnableWindow(g_app->peerCheckButton, enabled);
    EnableWindow(g_app->searchButton, enabled);
    EnableWindow(g_app->logoutButton, enabled);
    ShowWindow(g_app->cancelButton, enabled ? SW_HIDE : SW_SHOW);
    EnableWindow(g_app->cancelButton, !enabled);
}

void setAuthenticatedUi(bool loggedIn) {
    ShowWindow(g_app->receiver, loggedIn ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app->address, loggedIn ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app->peerPort, loggedIn ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app->filePath, loggedIn ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app->sessionKey, loggedIn ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app->browseButton, loggedIn ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app->sendButton, loggedIn ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app->pendingButton, loggedIn ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app->historyButton, loggedIn ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app->logoutButton, loggedIn ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app->searchUser, loggedIn ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app->searchButton, loggedIn ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app->peerCheckButton, loggedIn ? SW_SHOW : SW_HIDE);
    if (!loggedIn) {
        ShowWindow(g_app->cancelButton, SW_HIDE);
        EnableWindow(g_app->cancelButton, FALSE);
    }

    EnableWindow(g_app->username, !loggedIn);
    EnableWindow(g_app->password, !loggedIn);
    EnableWindow(g_app->listenPort, !loggedIn);
    EnableWindow(g_app->loginButton, !loggedIn);
    EnableWindow(g_app->registerButton, !loggedIn);
    if (loggedIn) {
        setOperationButtons(true);
    }
}

HWND addControl(HWND parent, const char* type, const char* text, DWORD style,
                int x, int y, int width, int height, int id) {
    return CreateWindowExA(0, type, text, WS_CHILD | WS_VISIBLE | style,
                           x, y, width, height, parent,
                           id == 0 ? NULL : reinterpret_cast<HMENU>(id),
                           GetModuleHandle(NULL), NULL);
}

void createControls(HWND hwnd) {
    addControl(hwnd, "STATIC", "Account", SS_LEFT, 20, 15, 100, 22, 0);
    addControl(hwnd, "STATIC", "Username", SS_LEFT, 20, 47, 70, 22, 0);
    g_app->username = addControl(hwnd, "EDIT", "", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
                                 95, 44, 190, 25, ID_USERNAME);
    addControl(hwnd, "STATIC", "Password", SS_LEFT, 20, 79, 70, 22, 0);
    g_app->password = addControl(hwnd, "EDIT", "", WS_TABSTOP | WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL,
                                 95, 76, 190, 25, ID_PASSWORD);
    addControl(hwnd, "STATIC", "Listen port", SS_LEFT, 315, 47, 75, 22, 0);
    g_app->listenPort = addControl(hwnd, "EDIT", "54000", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
                                   395, 44, 90, 25, ID_PORT);
    g_app->loginButton = addControl(hwnd, "BUTTON", "Login", WS_TABSTOP | BS_PUSHBUTTON,
                                    95, 111, 90, 30, ID_LOGIN);
    g_app->registerButton = addControl(hwnd, "BUTTON", "Register", WS_TABSTOP | BS_PUSHBUTTON,
                                       195, 111, 90, 30, ID_REGISTER);

    addControl(hwnd, "STATIC", "Send a file", SS_LEFT, 20, 164, 120, 22, 0);
    addControl(hwnd, "STATIC", "Receiver", SS_LEFT, 20, 196, 70, 22, 0);
    g_app->receiver = addControl(hwnd, "EDIT", "", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
                                 95, 193, 190, 25, ID_RECEIVER);
    addControl(hwnd, "STATIC", "Peer IP", SS_LEFT, 315, 196, 70, 22, 0);
    g_app->address = addControl(hwnd, "EDIT", "", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
                                385, 193, 190, 25, ID_ADDRESS);
    addControl(hwnd, "STATIC", "Peer port", SS_LEFT, 20, 228, 70, 22, 0);
    g_app->peerPort = addControl(hwnd, "EDIT", "", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
                                 95, 225, 90, 25, ID_PEER_PORT);
    addControl(hwnd, "STATIC", "File", SS_LEFT, 20, 260, 70, 22, 0);
    g_app->filePath = addControl(hwnd, "EDIT", "", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
                                 95, 257, 420, 25, ID_FILE_PATH);
    g_app->browseButton = addControl(hwnd, "BUTTON", "Browse...", WS_TABSTOP | BS_PUSHBUTTON,
                                     525, 255, 90, 29, ID_BROWSE);
    addControl(hwnd, "STATIC", "Shared key", SS_LEFT, 20, 292, 70, 22, 0);
    g_app->sessionKey = addControl(hwnd, "EDIT", "", WS_TABSTOP | WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL,
                                   95, 289, 220, 25, ID_SESSION_KEY);

    g_app->sendButton = addControl(hwnd, "BUTTON", "Send file", WS_TABSTOP | BS_PUSHBUTTON,
                                   95, 326, 100, 31, ID_SEND);
    g_app->pendingButton = addControl(hwnd, "BUTTON", "Review requests", WS_TABSTOP | BS_PUSHBUTTON,
                                      205, 326, 125, 31, ID_PENDING);
    g_app->peerCheckButton = addControl(hwnd, "BUTTON", "Check peer", WS_TABSTOP | BS_PUSHBUTTON,
                                        340, 326, 100, 31, ID_PEER_CHECK);
    g_app->historyButton = addControl(hwnd, "BUTTON", "History", WS_TABSTOP | BS_PUSHBUTTON,
                                      450, 326, 80, 31, ID_HISTORY);
    g_app->logoutButton = addControl(hwnd, "BUTTON", "Logout", WS_TABSTOP | BS_PUSHBUTTON,
                                     540, 326, 75, 31, ID_LOGOUT);

    addControl(hwnd, "STATIC", "Search history for username", SS_LEFT, 20, 373, 190, 22, 0);
    g_app->searchUser = addControl(hwnd, "EDIT", "", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
                                   210, 370, 190, 25, ID_SEARCH);
    g_app->searchButton = addControl(hwnd, "BUTTON", "Search", WS_TABSTOP | BS_PUSHBUTTON,
                                     410, 368, 80, 29, ID_SEARCH_BUTTON);
    g_app->cancelButton = addControl(hwnd, "BUTTON", "Cancel transfer",
                                     WS_TABSTOP | BS_PUSHBUTTON,
                                     500, 368, 115, 29, ID_CANCEL);

    addControl(hwnd, "STATIC", "Status", SS_LEFT, 20, 418, 70, 22, 0);
    g_app->status = addControl(hwnd, "STATIC", "Offline", SS_LEFT, 95, 418, 500, 22, 0);
    addControl(hwnd, "STATIC", "Activity", SS_LEFT, 20, 450, 100, 22, 0);
    g_app->log = addControl(hwnd, "LISTBOX", "", WS_VSCROLL | WS_BORDER | LBS_HASSTRINGS,
                            20, 475, 600, 170, 0);
    setAuthenticatedUi(false);
}

void finishWorker(OperationResult* result) {
    if (g_app->worker != NULL) {
        WaitForSingleObject(g_app->worker, INFINITE);
        CloseHandle(g_app->worker);
        g_app->worker = NULL;
    }
    g_app->operationRunning = false;
    setOperationButtons(true);
    appendLog(result->message);
    setStatus(result->message);
    if (!result->success) {
        showError("Operation failed", result->message);
    } else {
        MessageBoxA(g_app->hwnd, result->message.c_str(), "Secure Share",
                    MB_OK | MB_ICONINFORMATION);
    }
    delete result;
    if (g_app->closing) {
        DestroyWindow(g_app->hwnd);
    }
}

unsigned __stdcall operationThread(void* context) {
    std::unique_ptr<WorkerArgs> args(static_cast<WorkerArgs*>(context));
    std::unique_ptr<OperationResult> result(new OperationResult());
    result->success = false;
    try {
        if (args->incoming) {
            std::string error;
            std::string savedFile;
            const bool ok = args->transfers->acceptIncoming(
                args->offer, args->receiver, args->key,
                [hwnd = args->hwnd, cancelRequested = args->cancelRequested]
                (std::uint64_t completed, std::uint64_t total) {
                    if (InterlockedCompareExchange(cancelRequested, 0, 0) != 0) {
                        throw std::runtime_error("Transfer cancelled by receiver.");
                    }
                    const WPARAM percent = total == 0 ? 100 :
                        static_cast<WPARAM>(completed * 100 / total);
                    PostMessageA(hwnd, WM_APP_TRANSFER_PROGRESS, percent, 0);
                }, error, savedFile);
            result->success = ok;
            result->message = ok ? "Received file saved to " + savedFile : error;
        } else {
            args->transfers->sendFileChecked(args->sender, args->receiver,
                args->address, args->port, args->path, args->key,
                args->cancelRequested);
            result->success = true;
            result->message = "Transfer to " + args->receiver + " completed.";
        }
    } catch (const std::exception& exception) {
        result->success = false;
        result->message = exception.what();
    } catch (...) {
        result->success = false;
        result->message = "Unexpected error during the transfer.";
    }
    PostMessageA(args->hwnd, WM_APP_OPERATION_DONE, 0,
                 reinterpret_cast<LPARAM>(result.release()));
    return 0;
}

bool startOperation(WorkerArgs* args) {
    if (g_app->operationRunning) {
        delete args;
        return false;
    }
    g_app->operationRunning = true;
    InterlockedExchange(&g_app->cancelRequested, 0);
    args->cancelRequested = &g_app->cancelRequested;
    setOperationButtons(false);
    setStatus(args->incoming ? "Receiving transfer..." : "Waiting for receiver...");
    unsigned int threadId = 0;
    const uintptr_t thread = _beginthreadex(NULL, 0, operationThread, args, 0, &threadId);
    if (thread == 0) {
        g_app->operationRunning = false;
        setOperationButtons(true);
        delete args;
        showError("Could not start operation", "Windows could not create the transfer worker thread.");
        return false;
    }
    g_app->worker = reinterpret_cast<HANDLE>(thread);
    return true;
}

void handleRegister() {
    try {
        g_app->auth.registerUser(getText(g_app->username), getText(g_app->password));
        appendLog("Registered account " + getText(g_app->username) + ".");
        MessageBoxA(g_app->hwnd, "Account registered. You can now log in.",
                    "Registration complete", MB_OK | MB_ICONINFORMATION);
    } catch (const std::exception& exception) {
        showError("Registration failed", exception.what());
    }
}

void handleLogin() {
    bool authenticated = false;
    try {
        const std::string username = getText(g_app->username);
        if (!g_app->auth.login(username, getText(g_app->password))) {
            showError("Login failed", "Invalid username or password.");
            return;
        }
        authenticated = true;
        const unsigned short port = parsePort(getText(g_app->listenPort), 54000);
        g_app->server.start(port, username);
        g_app->currentUser = username;
        g_app->transfers.reset(new TransferManager(g_app->server, g_app->history));
        setAuthenticatedUi(true);
        setStatus("Online as " + username + " on TCP port " + std::to_string(port) + ".");
        appendLog("Logged in as " + username + ".");
    } catch (const std::exception& exception) {
        if (authenticated) {
            g_app->auth.logout();
            g_app->currentUser.clear();
            g_app->server.stop();
        }
        showError("Login failed", exception.what());
    }
}

void handleBrowse() {
    char filename[MAX_PATH] = {};
    OPENFILENAMEA dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_app->hwnd;
    dialog.lpstrFilter = "All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&dialog)) {
        SetWindowTextA(g_app->filePath, filename);
    }
}

void handleSend() {
    try {
        const std::string receiver = getText(g_app->receiver);
        const std::string address = getText(g_app->address);
        const unsigned short port = parsePort(getText(g_app->peerPort), 54001);
        const std::string path = getText(g_app->filePath);
        const std::string key = getText(g_app->sessionKey);
        if (receiver.empty() || address.empty() || path.empty()) {
            throw std::invalid_argument("Enter the receiver, peer IP, and choose a file.");
        }
        if (key.size() < 8) {
            throw std::invalid_argument("The shared session key must contain at least 8 characters.");
        }
        WorkerArgs* args = new WorkerArgs();
        args->hwnd = g_app->hwnd;
        args->transfers = g_app->transfers.get();
        args->sender = g_app->currentUser;
        args->receiver = receiver;
        args->address = address;
        args->port = port;
        args->path = path;
        args->key = key;
        args->incoming = false;
        startOperation(args);
    } catch (const std::exception& exception) {
        showError("Cannot send file", exception.what());
    }
}

void handlePending() {
    const std::vector<IncomingOffer> offers = g_app->transfers->pendingOffers();
    if (offers.empty()) {
        MessageBoxA(g_app->hwnd, "No incoming transfer requests are waiting.",
                    "Incoming requests", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const IncomingOffer& offer = offers.front();
    std::ostringstream prompt;
    prompt << offers.size() << " pending request(s)\n\n"
           << "Sender: " << offer.sender << "\nFile: " << offer.filename << "\n"
           << "Size: " << offer.fileSize << " bytes\n\nAccept this transfer?";
    const int decision = MessageBoxA(g_app->hwnd, prompt.str().c_str(),
                                     "Incoming transfer", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (decision == IDCANCEL) {
        return;
    }
    if (decision == IDNO) {
        std::string error;
        if (!g_app->transfers->rejectIncoming(offer, error)) {
            showError("Could not reject request", error);
            return;
        }
        appendLog("Rejected transfer from " + offer.sender + ".");
        return;
    }

    const std::string key = getText(g_app->sessionKey);
    if (!EncryptionManager::verifySessionKey(key, offer.nonce, offer.proof)) {
        showError("Session key verification failed",
                  "The shared session key did not match. The request remains pending.");
        return;
    }
    WorkerArgs* args = new WorkerArgs();
    args->hwnd = g_app->hwnd;
    args->transfers = g_app->transfers.get();
    args->offer = offer;
    args->receiver = g_app->currentUser;
    args->key = key;
    args->incoming = true;
    startOperation(args);
}

void handleHistory(bool search) {
    std::vector<TransferRecord> records = search
        ? g_app->history.searchByUsername(getText(g_app->searchUser))
        : g_app->history.chronological();
    if (records.empty()) {
        MessageBoxA(g_app->hwnd, search ? "No records found for that username."
                                       : "No transfer history is available.",
                    "Transfer history", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::ostringstream text;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const TransferRecord& item = records[i];
        text << item.timestamp << " | " << item.sender << " -> " << item.receiver
             << " | " << item.filename << " | " << item.fileSize << " bytes | "
             << item.status << "\r\n";
    }
    MessageBoxA(g_app->hwnd, text.str().c_str(),
                search ? "History search results" : "Transfer history",
                MB_OK | MB_ICONINFORMATION);
}

void handlePeerCheck() {
    try {
        const unsigned short port = parsePort(getText(g_app->peerPort), 54001);
        Client client;
        const bool online = client.isReachable(getText(g_app->address), port);
        const std::string result = online ? "Peer is online." : "Peer is offline or unreachable.";
        setStatus(result);
        appendLog(result);
    } catch (const std::exception& exception) {
        showError("Peer check failed", exception.what());
    }
}

void handleLogout() {
    if (g_app->operationRunning) {
        MessageBoxA(g_app->hwnd, "Wait for the active transfer to finish before logging out.",
                    "Transfer in progress", MB_OK | MB_ICONWARNING);
        return;
    }
    g_app->server.stop();
    g_app->auth.logout();
    g_app->transfers.reset();
    g_app->currentUser.clear();
    setAuthenticatedUi(false);
    setStatus("Offline");
    appendLog("Logged out; listener stopped.");
}

void handleCancel() {
    if (g_app->operationRunning) {
        InterlockedExchange(&g_app->cancelRequested, 1);
        setStatus("Cancelling transfer...");
        appendLog("Cancellation requested.");
    }
}

LRESULT CALLBACK windowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        g_app->hwnd = hwnd;
        createControls(hwnd);
        return 0;
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            switch (LOWORD(wParam)) {
            case ID_LOGIN: handleLogin(); return 0;
            case ID_REGISTER: handleRegister(); return 0;
            case ID_BROWSE: handleBrowse(); return 0;
            case ID_SEND: handleSend(); return 0;
            case ID_PENDING: handlePending(); return 0;
            case ID_HISTORY: handleHistory(false); return 0;
            case ID_LOGOUT: handleLogout(); return 0;
            case ID_SEARCH_BUTTON: handleHistory(true); return 0;
            case ID_PEER_CHECK: handlePeerCheck(); return 0;
            case ID_CANCEL: handleCancel(); return 0;
            }
        }
        break;
    case WM_APP_TRANSFER_PROGRESS: {
        const std::string messageText = "Transfer progress: " +
            std::to_string(static_cast<unsigned int>(wParam)) + "%";
        setStatus(messageText);
        return 0;
    }
    case WM_APP_OPERATION_DONE:
        finishWorker(reinterpret_cast<OperationResult*>(lParam));
        return 0;
    case WM_CLOSE:
        if (g_app->operationRunning) {
            g_app->closing = true;
            EnableWindow(g_app->hwnd, FALSE);
            setStatus("Closing after the active transfer finishes...");
            return 0;
        }
        if (g_app->server.isRunning()) {
            g_app->server.stop();
        }
        g_app->auth.logout();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, message, wParam, lParam);
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    try {
        WinsockRuntime winsock;
        FileManager::ensureDirectory("data");
        AppState app;
        g_app = &app;

        WNDCLASSA windowClass = {};
        windowClass.lpfnWndProc = windowProcedure;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = "SecureShareMainWindow";
        if (!RegisterClassA(&windowClass)) {
            throw std::runtime_error("Could not register the application window.");
        }
        HWND hwnd = CreateWindowExA(0, windowClass.lpszClassName,
            "Secure Share - Secure File Sharing", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 680, 720, NULL, NULL, instance, NULL);
        if (hwnd == NULL) {
            throw std::runtime_error("Could not create the application window.");
        }
        ShowWindow(hwnd, showCommand);
        UpdateWindow(hwnd);

        MSG msg;
        while (GetMessageA(&msg, NULL, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        return static_cast<int>(msg.wParam);
    } catch (const std::exception& exception) {
        MessageBoxA(NULL, exception.what(), "Secure Share startup error",
                    MB_OK | MB_ICONERROR);
        return 1;
    }
}
