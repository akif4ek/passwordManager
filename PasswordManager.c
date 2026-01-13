#include <windows.h>
#include <commctrl.h>
#include <wincrypt.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")

#ifndef CALG_SHA_256
#define CALG_SHA_256 (ALG_CLASS_HASH | ALG_TYPE_ANY | ALG_SID_SHA_256)
#endif
#ifndef ALG_SID_SHA_256
#define ALG_SID_SHA_256 12
#endif

// Define ListView extended styles if not available
#ifndef LVS_EX_FULLROWSELECT
#define LVS_EX_FULLROWSELECT 0x00000020
#endif
#ifndef LVS_EX_GRIDLINES
#define LVS_EX_GRIDLINES 0x00000001
#endif
#ifndef ListView_SetExtendedListViewStyle
#define ListView_SetExtendedListViewStyle(hwnd, style) \
    SendMessage((hwnd), LVM_SETEXTENDEDLISTVIEWSTYLE, 0, (LPARAM)(style))
#endif
#ifndef LVM_SETEXTENDEDLISTVIEWSTYLE
#define LVM_SETEXTENDEDLISTVIEWSTYLE (LVM_FIRST + 54)
#endif

#define MAX_ENTRIES 100
#define MAX_STRING 256
#define AES_KEY_SIZE 32
#define SALT_SIZE 16

typedef struct {
    char service[MAX_STRING];
    char username[MAX_STRING];
    char password[MAX_STRING];
} PasswordEntry;

typedef struct {
    PasswordEntry entries[MAX_ENTRIES];
    int count;
    BYTE masterKey[AES_KEY_SIZE];
    BYTE salt[SALT_SIZE];
    BOOL unlocked;
} PasswordVault;

PasswordVault vault = {0};
HWND hMainWnd, hListView, hServiceEdit, hUsernameEdit, hPasswordEdit, hSearchEdit;
HFONT hFont, hTitleFont;
const char* DATA_FILE = "passwords.dat";

BOOL DeriveKey(const char* password, BYTE* salt, BYTE* key) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BOOL result = FALSE;
    
    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return FALSE;
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return FALSE;
    }
    
    CryptHashData(hHash, (BYTE*)password, strlen(password), 0);
    CryptHashData(hHash, salt, SALT_SIZE, 0);
    
    DWORD keyLen = AES_KEY_SIZE;
    if (CryptGetHashParam(hHash, HP_HASHVAL, key, &keyLen, 0)) result = TRUE;
    
    if (hHash) CryptDestroyHash(hHash);
    if (hProv) CryptReleaseContext(hProv, 0);
    return result;
}

BOOL EncryptData(BYTE* key, BYTE* data, DWORD dataLen, BYTE* output, DWORD* outLen) {
    HCRYPTPROV hProv = 0;
    HCRYPTKEY hKey = 0;
    HCRYPTHASH hHash = 0;
    BOOL result = FALSE;
    
    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return FALSE;
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return FALSE;
    }
    
    CryptHashData(hHash, key, AES_KEY_SIZE, 0);
    
    if (CryptDeriveKey(hProv, CALG_AES_256, hHash, 0, &hKey)) {
        memcpy(output, data, dataLen);
        *outLen = dataLen;
        if (CryptEncrypt(hKey, 0, TRUE, 0, output, outLen, dataLen + 16)) result = TRUE;
        CryptDestroyKey(hKey);
    }
    
    if (hHash) CryptDestroyHash(hHash);
    if (hProv) CryptReleaseContext(hProv, 0);
    return result;
}

BOOL DecryptData(BYTE* key, BYTE* data, DWORD dataLen, BYTE* output, DWORD* outLen) {
    HCRYPTPROV hProv = 0;
    HCRYPTKEY hKey = 0;
    HCRYPTHASH hHash = 0;
    BOOL result = FALSE;
    
    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return FALSE;
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return FALSE;
    }
    
    CryptHashData(hHash, key, AES_KEY_SIZE, 0);
    
    if (CryptDeriveKey(hProv, CALG_AES_256, hHash, 0, &hKey)) {
        memcpy(output, data, dataLen);
        *outLen = dataLen;
        if (CryptDecrypt(hKey, 0, TRUE, 0, output, outLen)) result = TRUE;
        CryptDestroyKey(hKey);
    }
    
    if (hHash) CryptDestroyHash(hHash);
    if (hProv) CryptReleaseContext(hProv, 0);
    return result;
}

void SaveVault() {
    FILE* f = fopen(DATA_FILE, "wb");
    if (!f) return;
    
    fwrite(vault.salt, 1, SALT_SIZE, f);
    
    BYTE encCount[16];
    DWORD encLen;
    EncryptData(vault.masterKey, (BYTE*)&vault.count, sizeof(int), encCount, &encLen);
    fwrite(&encLen, sizeof(DWORD), 1, f);
    fwrite(encCount, 1, encLen, f);
    
    for (int i = 0; i < vault.count; i++) {
        BYTE encEntry[sizeof(PasswordEntry) + 16];
        DWORD encLen;
        EncryptData(vault.masterKey, (BYTE*)&vault.entries[i], sizeof(PasswordEntry), encEntry, &encLen);
        fwrite(&encLen, sizeof(DWORD), 1, f);
        fwrite(encEntry, 1, encLen, f);
    }
    fclose(f);
}

BOOL LoadVault() {
    FILE* f = fopen(DATA_FILE, "rb");
    if (!f) return FALSE;
    
    fread(vault.salt, 1, SALT_SIZE, f);
    
    DWORD encLen;
    BYTE encCount[16];
    fread(&encLen, sizeof(DWORD), 1, f);
    fread(encCount, 1, encLen, f);
    
    BYTE decCount[16];
    DWORD decLen;
    if (!DecryptData(vault.masterKey, encCount, encLen, decCount, &decLen)) {
        fclose(f);
        return FALSE;
    }
    memcpy(&vault.count, decCount, sizeof(int));
    
    for (int i = 0; i < vault.count; i++) {
        BYTE encEntry[sizeof(PasswordEntry) + 16];
        fread(&encLen, sizeof(DWORD), 1, f);
        fread(encEntry, 1, encLen, f);
        
        BYTE decEntry[sizeof(PasswordEntry) + 16];
        DecryptData(vault.masterKey, encEntry, encLen, decEntry, &decLen);
        memcpy(&vault.entries[i], decEntry, sizeof(PasswordEntry));
    }
    
    fclose(f);
    return TRUE;
}

void RefreshListView() {
    ListView_DeleteAllItems(hListView);
    
    char searchText[MAX_STRING] = {0};
    if (hSearchEdit) GetWindowText(hSearchEdit, searchText, MAX_STRING);
    
    for (int i = 0; i < vault.count; i++) {
        if (strlen(searchText) > 0 && strstr(vault.entries[i].service, searchText) == NULL &&
            strstr(vault.entries[i].username, searchText) == NULL) continue;
        
        LVITEM lvi = {0};
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = ListView_GetItemCount(hListView);
        lvi.lParam = i;
        lvi.pszText = vault.entries[i].service;
        int idx = ListView_InsertItem(hListView, &lvi);
        ListView_SetItemText(hListView, idx, 1, vault.entries[i].username);
        ListView_SetItemText(hListView, idx, 2, "********");
    }
}

void GeneratePassword(char* output, int length) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*";
    HCRYPTPROV hProv;
    CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
    for (int i = 0; i < length; i++) {
        BYTE rnd;
        CryptGenRandom(hProv, 1, &rnd);
        output[i] = charset[rnd % (sizeof(charset) - 1)];
    }
    output[length] = '\0';
    CryptReleaseContext(hProv, 0);
}

void CopyToClipboard(const char* text) {
    if (OpenClipboard(hMainWnd)) {
        EmptyClipboard();
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, strlen(text) + 1);
        if (hMem) {
            memcpy(GlobalLock(hMem), text, strlen(text) + 1);
            GlobalUnlock(hMem);
            SetClipboardData(CF_TEXT, hMem);
        }
        CloseClipboard();
    }
}

void AddPassword() {
    if (vault.count >= MAX_ENTRIES) {
        MessageBox(hMainWnd, "Maximum entries reached!", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    char service[MAX_STRING], username[MAX_STRING], password[MAX_STRING];
    GetWindowText(hServiceEdit, service, MAX_STRING);
    GetWindowText(hUsernameEdit, username, MAX_STRING);
    GetWindowText(hPasswordEdit, password, MAX_STRING);
    
    if (strlen(service) == 0 || strlen(password) == 0) {
        MessageBox(hMainWnd, "Service and Password are required!", "Error", MB_OK | MB_ICONWARNING);
        return;
    }
    
    strcpy(vault.entries[vault.count].service, service);
    strcpy(vault.entries[vault.count].username, username);
    strcpy(vault.entries[vault.count].password, password);
    vault.count++;
    
    SaveVault();
    RefreshListView();
    
    SetWindowText(hServiceEdit, "");
    SetWindowText(hUsernameEdit, "");
    SetWindowText(hPasswordEdit, "");
    MessageBox(hMainWnd, "Password added successfully!", "Success", MB_OK | MB_ICONINFORMATION);
}

void DeletePassword() {
    int sel = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
    if (sel == -1) {
        MessageBox(hMainWnd, "Please select an entry!", "Error", MB_OK | MB_ICONWARNING);
        return;
    }
    
    LVITEM lvi = {0};
    lvi.mask = LVIF_PARAM;
    lvi.iItem = sel;
    ListView_GetItem(hListView, &lvi);
    int actualIndex = lvi.lParam;
    
    if (MessageBox(hMainWnd, "Delete this entry?", "Confirm", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        for (int i = actualIndex; i < vault.count - 1; i++) vault.entries[i] = vault.entries[i + 1];
        vault.count--;
        SaveVault();
        RefreshListView();
    }
}

void ShowPassword() {
    int sel = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
    if (sel == -1) {
        MessageBox(hMainWnd, "Please select an entry!", "Error", MB_OK | MB_ICONWARNING);
        return;
    }
    
    LVITEM lvi = {0};
    lvi.mask = LVIF_PARAM;
    lvi.iItem = sel;
    ListView_GetItem(hListView, &lvi);
    int actualIndex = lvi.lParam;
    
    char msg[MAX_STRING * 4];
    sprintf(msg, "Service: %s\n\nUsername: %s\n\nPassword: %s", 
            vault.entries[actualIndex].service, 
            vault.entries[actualIndex].username, 
            vault.entries[actualIndex].password);
    MessageBox(hMainWnd, msg, "Password Details", MB_OK | MB_ICONINFORMATION);
}

void CopyUsername() {
    int sel = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
    if (sel == -1) return;
    LVITEM lvi = {0};
    lvi.mask = LVIF_PARAM;
    lvi.iItem = sel;
    ListView_GetItem(hListView, &lvi);
    CopyToClipboard(vault.entries[lvi.lParam].username);
    MessageBox(hMainWnd, "Username copied!", "Success", MB_OK | MB_ICONINFORMATION);
}

void CopyPassword() {
    int sel = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
    if (sel == -1) return;
    LVITEM lvi = {0};
    lvi.mask = LVIF_PARAM;
    lvi.iItem = sel;
    ListView_GetItem(hListView, &lvi);
    CopyToClipboard(vault.entries[lvi.lParam].password);
    MessageBox(hMainWnd, "Password copied!", "Success", MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            hFont = CreateFont(15, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, "Segoe UI");
            hTitleFont = CreateFont(20, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0, 0, "Segoe UI");
            
            HWND hTitle = CreateWindow("STATIC", "Secure Password Manager", WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 10, 780, 30, hwnd, NULL, NULL, NULL);
            SendMessage(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
            
            CreateWindow("STATIC", "Search:", WS_CHILD | WS_VISIBLE, 20, 50, 60, 20, hwnd, NULL, NULL, NULL);
            hSearchEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                85, 48, 200, 24, hwnd, (HMENU)2005, NULL, NULL);
            SendMessage(hSearchEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            hListView = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, "",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                20, 85, 740, 250, hwnd, (HMENU)2001, NULL, NULL);
            SendMessage(hListView, WM_SETFONT, (WPARAM)hFont, TRUE);
            ListView_SetExtendedListViewStyle(hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            
            LVCOLUMN lvc = {0};
            lvc.mask = LVCF_TEXT | LVCF_WIDTH;
            lvc.cx = 240;
            lvc.pszText = "Service/Website";
            ListView_InsertColumn(hListView, 0, &lvc);
            lvc.cx = 240;
            lvc.pszText = "Username/Email";
            ListView_InsertColumn(hListView, 1, &lvc);
            lvc.cx = 240;
            lvc.pszText = "Password";
            ListView_InsertColumn(hListView, 2, &lvc);
            
            HWND hLabel = CreateWindow("STATIC", "Add New Entry", WS_CHILD | WS_VISIBLE | SS_CENTER,
                20, 350, 740, 25, hwnd, NULL, NULL, NULL);
            SendMessage(hLabel, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
            
            CreateWindow("STATIC", "Service:", WS_CHILD | WS_VISIBLE, 20, 385, 100, 20, hwnd, NULL, NULL, NULL);
            hServiceEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                130, 383, 180, 24, hwnd, (HMENU)2002, NULL, NULL);
            SendMessage(hServiceEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            CreateWindow("STATIC", "Username:", WS_CHILD | WS_VISIBLE, 330, 385, 100, 20, hwnd, NULL, NULL, NULL);
            hUsernameEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                440, 383, 180, 24, hwnd, (HMENU)2003, NULL, NULL);
            SendMessage(hUsernameEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            CreateWindow("STATIC", "Password:", WS_CHILD | WS_VISIBLE, 20, 420, 100, 20, hwnd, NULL, NULL, NULL);
            hPasswordEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                130, 418, 180, 24, hwnd, (HMENU)2004, NULL, NULL);
            SendMessage(hPasswordEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            HWND hGenBtn = CreateWindow("BUTTON", "Generate", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                320, 418, 100, 26, hwnd, (HMENU)1008, NULL, NULL);
            SendMessage(hGenBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            HWND hAddBtn = CreateWindow("BUTTON", "Add Entry", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                430, 418, 100, 26, hwnd, (HMENU)1001, NULL, NULL);
            SendMessage(hAddBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            HWND hShowBtn = CreateWindow("BUTTON", "Show Password", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                20, 460, 120, 30, hwnd, (HMENU)1003, NULL, NULL);
            SendMessage(hShowBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            HWND hDelBtn = CreateWindow("BUTTON", "Delete", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                150, 460, 100, 30, hwnd, (HMENU)1002, NULL, NULL);
            SendMessage(hDelBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            HWND hCopyUserBtn = CreateWindow("BUTTON", "Copy Username", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                260, 460, 130, 30, hwnd, (HMENU)1005, NULL, NULL);
            SendMessage(hCopyUserBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            HWND hCopyPassBtn = CreateWindow("BUTTON", "Copy Password", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                400, 460, 130, 30, hwnd, (HMENU)1006, NULL, NULL);
            SendMessage(hCopyPassBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            RefreshListView();
            break;
        }
        
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case 1001: AddPassword(); break;
                case 1002: DeletePassword(); break;
                case 1003: ShowPassword(); break;
                case 1005: CopyUsername(); break;
                case 1006: CopyPassword(); break;
                case 1008: {
                    char gen[25];
                    GeneratePassword(gen, 16);
                    SetWindowText(hPasswordEdit, gen);
                    break;
                }
                case 2005:
                    if (HIWORD(wParam) == EN_CHANGE) RefreshListView();
                    break;
            }
            break;
        }
        
        case WM_CLOSE:
            if (MessageBox(hwnd, "Exit?", "Confirm", MB_YESNO | MB_ICONQUESTION) == IDYES)
                DestroyWindow(hwnd);
            break;
            
        case WM_DESTROY:
            DeleteObject(hFont);
            DeleteObject(hTitleFont);
            PostQuitMessage(0);
            break;
            
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

INT_PTR CALLBACK MasterDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowText(hDlg, "Master Password");
            RECT rc;
            GetWindowRect(hDlg, &rc);
            SetWindowPos(hDlg, NULL, (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2,
                        (GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2, 0, 0, SWP_NOSIZE);
            SetFocus(GetDlgItem(hDlg, 101));
            return TRUE;
        }
        
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == 102) {
                char password[MAX_STRING];
                GetDlgItemText(hDlg, 101, password, MAX_STRING);
                
                if (strlen(password) == 0) {
                    MessageBox(hDlg, "Password cannot be empty!", "Error", MB_OK | MB_ICONERROR);
                    return TRUE;
                }
                
                FILE* f = fopen(DATA_FILE, "rb");
                BOOL isNew = (f == NULL);
                if (f) fclose(f);
                
                if (isNew) {
                    HCRYPTPROV hProv;
                    CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
                    CryptGenRandom(hProv, SALT_SIZE, vault.salt);
                    CryptReleaseContext(hProv, 0);
                    
                    DeriveKey(password, vault.salt, vault.masterKey);
                    vault.count = 0;
                    vault.unlocked = TRUE;
                    SaveVault();
                    EndDialog(hDlg, 1);
                } else {
                    f = fopen(DATA_FILE, "rb");
                    fread(vault.salt, 1, SALT_SIZE, f);
                    fclose(f);
                    
                    DeriveKey(password, vault.salt, vault.masterKey);
                    
                    if (LoadVault()) {
                        vault.unlocked = TRUE;
                        EndDialog(hDlg, 1);
                    } else {
                        MessageBox(hDlg, "Incorrect password!", "Error", MB_OK | MB_ICONERROR);
                        SetDlgItemText(hDlg, 101, "");
                    }
                }
                return TRUE;
            }
            break;
            
        case WM_CLOSE:
            EndDialog(hDlg, 0);
            return TRUE;
    }
    return FALSE;
}

LRESULT CALLBACK LoginWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hEdit;
    
    switch (msg) {
        case WM_CREATE:
            CreateWindow("STATIC", "Enter Master Password:", WS_CHILD | WS_VISIBLE,
                20, 20, 350, 20, hwnd, NULL, NULL, NULL);
            
            hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", 
                WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL,
                20, 50, 350, 25, hwnd, (HMENU)101, NULL, NULL);
            
            CreateWindow("BUTTON", "Unlock", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                150, 90, 100, 30, hwnd, (HMENU)102, NULL, NULL);
            
            SetFocus(hEdit);
            return 0;
            
        case WM_COMMAND:
            if (LOWORD(wParam) == 102) {
                char password[MAX_STRING];
                GetWindowText(hEdit, password, MAX_STRING);
                
                if (strlen(password) == 0) {
                    MessageBox(hwnd, "Password cannot be empty!", "Error", MB_OK | MB_ICONERROR);
                    return 0;
                }
                
                FILE* f = fopen(DATA_FILE, "rb");
                BOOL isNew = (f == NULL);
                if (f) fclose(f);
                
                if (isNew) {
                    HCRYPTPROV hProv;
                    CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
                    CryptGenRandom(hProv, SALT_SIZE, vault.salt);
                    CryptReleaseContext(hProv, 0);
                    
                    DeriveKey(password, vault.salt, vault.masterKey);
                    vault.count = 0;
                    vault.unlocked = TRUE;
                    SaveVault();
                    PostQuitMessage(0);
                } else {
                    f = fopen(DATA_FILE, "rb");
                    fread(vault.salt, 1, SALT_SIZE, f);
                    fclose(f);
                    
                    DeriveKey(password, vault.salt, vault.masterKey);
                    
                    if (LoadVault()) {
                        vault.unlocked = TRUE;
                        PostQuitMessage(0);
                    } else {
                        MessageBox(hwnd, "Incorrect password!", "Error", MB_OK | MB_ICONERROR);
                        SetWindowText(hEdit, "");
                    }
                }
            }
            return 0;
            
        case WM_CLOSE:
            vault.unlocked = FALSE;
            PostQuitMessage(0);
            return 0;
            
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    // Register login window class
    WNDCLASSEX wcLogin = {0};
    wcLogin.cbSize = sizeof(WNDCLASSEX);
    wcLogin.lpfnWndProc = LoginWndProc;
    wcLogin.hInstance = hInst;
    wcLogin.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcLogin.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcLogin.lpszClassName = "LoginWindow";
    RegisterClassEx(&wcLogin);
    
    // Create login window
    HWND hLoginWnd = CreateWindowEx(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, 
        "LoginWindow", "Master Password",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 
        0, 0, 400, 180, NULL, NULL, hInst, NULL);
    
    // Center the window
    RECT rc;
    GetWindowRect(hLoginWnd, &rc);
    SetWindowPos(hLoginWnd, NULL, 
        (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2, 
        0, 0, SWP_NOSIZE);
    
    // Message loop for login window
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Destroy login window
    DestroyWindow(hLoginWnd);
    
    if (!vault.unlocked) return 0;
    
    // Register main window class
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "PasswordManager";
    RegisterClassEx(&wc);
    
    InitCommonControls();
    
    // Create main window
    hMainWnd = CreateWindowEx(0, "PasswordManager", "Password Manager - AES-256 Encrypted",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 560, NULL, NULL, hInst, NULL);
    
    if (!hMainWnd) {
        MessageBox(NULL, "Failed to create window!", "Error", MB_OK | MB_ICONERROR);
        return 0;
    }
    
    ShowWindow(hMainWnd, nShow);
    UpdateWindow(hMainWnd);
    
    // Main message loop
    MSG mainMsg;
    while (GetMessage(&mainMsg, NULL, 0, 0)) {
        TranslateMessage(&mainMsg);
        DispatchMessage(&mainMsg);
    }
    
    return mainMsg.wParam;
}
