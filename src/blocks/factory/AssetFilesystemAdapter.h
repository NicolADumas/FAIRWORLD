#pragma once

#include <string>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <system_error>
#include <iostream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fw {

// =============================================================================
// BM-F4.3.1: ASSET FILESYSTEM ADAPTER
// Fornisce operazioni atomiche, safe recovery contro path traversal / junction e
// distinzione esplicita tra process-crash consistency e power-loss durability.
// =============================================================================

class AssetFilesystemAdapter {
public:
    // Atomic replace: sostituisce dst con src in modo atomico a livello di filesystem.
    // Su Windows: usa MoveFileExW con MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH.
    // Su POSIX: usa rename().
    static bool AtomicReplace(
        const std::filesystem::path& src,
        const std::filesystem::path& dst,
        std::string* outError = nullptr
    ) {
        std::error_code ec;
        if (!std::filesystem::exists(src, ec)) {
            if (outError) *outError = "Sorgente non esistente: " + src.string();
            return false;
        }

#if defined(_WIN32)
        std::wstring wsrc = src.wstring();
        std::wstring wdst = dst.wstring();
        BOOL ok = ::MoveFileExW(wsrc.c_str(), wdst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!ok) {
            DWORD err = ::GetLastError();
            if (outError) *outError = "MoveFileExW fallito con codice errore: " + std::to_string(err);
            return false;
        }
        return true;
#else
        std::filesystem::rename(src, dst, ec);
        if (ec) {
            if (outError) *outError = "std::filesystem::rename fallito: " + ec.message();
            return false;
        }
        return true;
#endif
    }

    // Scrittura sicura con atomic replace da file temporaneo dedicato.
    // syncHardware = true attiva il flush a basso livello verso supporto fisico non volatile.
    static bool DurableWriteFile(
        const std::filesystem::path& dst,
        const std::string& content,
        bool syncHardware = false,
        std::string* outError = nullptr
    ) {
        try {
            std::error_code ec;
            std::filesystem::path parentDir = dst.parent_path();
            if (!parentDir.empty()) {
                std::filesystem::create_directories(parentDir, ec);
            }

            static uint64_t s_writeCounter = 0;
            auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            std::filesystem::path tempPath = dst;
            tempPath += ".tmp." + std::to_string(now) + "_" + std::to_string(++s_writeCounter);

            {
                std::ofstream out(tempPath, std::ios::binary);
                if (!out.is_open()) {
                    if (outError) *outError = "Impossibile aprire file temporaneo: " + tempPath.string();
                    return false;
                }
                out.write(content.data(), static_cast<std::streamsize>(content.size()));
                out.flush();
                if (!out.good()) {
                    out.close();
                    std::filesystem::remove(tempPath, ec);
                    if (outError) *outError = "Errore durante flush stream: " + tempPath.string();
                    return false;
                }
                out.close();
            }

#if defined(_WIN32)
            if (syncHardware) {
                HANDLE h = ::CreateFileW(
                    tempPath.wstring().c_str(),
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr
                );
                if (h != INVALID_HANDLE_VALUE) {
                    ::FlushFileBuffers(h);
                    ::CloseHandle(h);
                }
            }
#endif

            if (!AtomicReplace(tempPath, dst, outError)) {
                std::filesystem::remove(tempPath, ec);
                return false;
            }
            return true;
        } catch (const std::exception& ex) {
            if (outError) *outError = std::string("Eccezione durante DurableWriteFile: ") + ex.what();
            return false;
        }
    }

    // Rileva symlink o directory junction (reparse point NTFS su Windows)
    static bool IsSymlinkOrJunction(const std::filesystem::path& path) {
        std::error_code ec;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(path, ec))) {
            return true;
        }
#if defined(_WIN32)
        DWORD attrs = ::GetFileAttributesW(path.wstring().c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
            return true;
        }
#endif
        return false;
    }

    // Verifica che target sia rigidamente contenuto all'interno di managedRoot.
    // Blocca path traversal ("..") e target esterni.
    static bool IsSafePathUnderRoot(
        const std::filesystem::path& target,
        const std::filesystem::path& managedRoot,
        std::string* outError = nullptr
    ) {
        std::error_code ec;
        if (managedRoot.empty()) {
            if (outError) *outError = "Managed root e' vuota.";
            return false;
        }

        std::filesystem::path rootCanon = std::filesystem::weakly_canonical(managedRoot, ec);
        if (ec) {
            if (outError) *outError = "Impossibile risolvere root canonica: " + ec.message();
            return false;
        }

        std::filesystem::path targetCanon = std::filesystem::weakly_canonical(target, ec);
        if (ec) {
            if (outError) *outError = "Impossibile risolvere target canonico: " + ec.message();
            return false;
        }

        auto rel = targetCanon.lexically_relative(rootCanon);
        if (rel.empty() || rel.string().rfind("..", 0) == 0) {
            if (outError) *outError = "Path traversal rilevato: " + target.string() + " non e' sotto " + managedRoot.string();
            return false;
        }

        return true;
    }

    // SafeRemoveAll: rimuove ricorsivamente solo all'interno di managedRoot.
    // Non segue symlink o directory junction verso directory esterne.
    static bool SafeRemoveAll(
        const std::filesystem::path& target,
        const std::filesystem::path& managedRoot,
        std::string* outError = nullptr
    ) {
        std::error_code ec;
        if (!std::filesystem::exists(target, ec) && !IsSymlinkOrJunction(target)) {
            return true;
        }

        if (!IsSafePathUnderRoot(target, managedRoot, outError)) {
            return false;
        }

        // Se target stesso e' un symlink o una junction, elimina solo il link, mai la destinazione esterna
        if (IsSymlinkOrJunction(target)) {
#if defined(_WIN32)
            if (std::filesystem::is_directory(std::filesystem::symlink_status(target, ec))) {
                if (!::RemoveDirectoryW(target.wstring().c_str())) {
                    DWORD err = ::GetLastError();
                    if (outError) *outError = "RemoveDirectoryW su junction fallito: " + std::to_string(err);
                    return false;
                }
                return true;
            }
#endif
            std::filesystem::remove(target, ec);
            if (ec) {
                if (outError) *outError = "Rimozione symlink fallita: " + ec.message();
                return false;
            }
            return true;
        }

        // Se e' una directory, iteriamo ricorsivamente eliminando prima i figli in modo sicuro
        if (std::filesystem::is_directory(target, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(target, ec)) {
                if (IsSymlinkOrJunction(entry.path())) {
#if defined(_WIN32)
                    if (std::filesystem::is_directory(std::filesystem::symlink_status(entry.path(), ec))) {
                        ::RemoveDirectoryW(entry.path().wstring().c_str());
                        continue;
                    }
#endif
                    std::filesystem::remove(entry.path(), ec);
                } else if (entry.is_directory(ec)) {
                    SafeRemoveAll(entry.path(), managedRoot, outError);
                } else {
                    std::filesystem::remove(entry.path(), ec);
                }
            }
            std::filesystem::remove(target, ec);
            return true;
        }

        std::filesystem::remove(target, ec);
        return true;
    }
};

} // namespace fw
