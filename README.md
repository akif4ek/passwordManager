# Secure Password Manager

![Windows](https://img.shields.io/badge/Platform-Windows-blue)
![C](https://img.shields.io/badge/Language-C-yellow)
![AES-256](https://img.shields.io/badge/Encryption-AES--256-green)
![License](https://img.shields.io/badge/License-MIT-orange)

A secure Windows desktop password manager application with AES-256 encryption for safely storing and managing your passwords.

## ✨ Features

### 🔒 Security
- **AES-256 Encryption** - Military-grade encryption for all stored data
- **Master Password Protection** - PBKDF2 key derivation with salt
- **Secure Password Generator** - Cryptographically random passwords
- **Encrypted Local Storage** - All data encrypted before saving to disk

### 💻 User Interface
- **Clean, Intuitive GUI** - Easy-to-use Windows interface
- **Search Functionality** - Instantly find passwords by service or username
- **Clipboard Integration** - One-click copy for usernames and passwords
- **Password Visibility Toggle** - View passwords when needed

### 🔧 Functionality
- **Add/Edit/Delete** - Full CRUD operations for password entries
- **Auto-generation** - Generate strong random passwords
- **Export/Import** - Backup and restore functionality
- **Password Strength Indicator** - Visual feedback on password strength

## 📋 Requirements

- **Operating System**: Windows 7 or later
- **Compiler**: MinGW GCC or Microsoft Visual Studio
- **Libraries**: Windows API, Common Controls, Cryptography API

## 🚀 Installation

### Option 1: Download Pre-compiled Binary
1. Download the latest release from the [Releases](https://github.com/yourusername/passwordmanager/releases) page
2. Extract the ZIP file
3. Run `PasswordManager.exe`

### Option 2: Build from Source

#### Using MinGW (Recommended):
```bash
# Install MinGW if not already installed
# Then compile:
gcc -o PasswordManager.exe PasswordManager.c -lcomctl32 -ladvapi32 -lgdi32 -mwindows -O2
