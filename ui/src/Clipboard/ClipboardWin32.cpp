#include "../../include/Clipboard/ClipboardWin32.hpp"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

namespace gsr {
    ClipboardWin32::ClipboardWin32() {

    }

    ClipboardWin32::~ClipboardWin32() {

    }

    void ClipboardWin32::set_current_file(const std::string &filepath, FileType file_type) {
        /* An empty path clears the clipboard (matches X11 unset semantics). */
        if(filepath.empty()) {
            if(OpenClipboard(NULL)) {
                EmptyClipboard();
                CloseClipboard();
            }
            return;
        }

        FILE *file = fopen(filepath.c_str(), "rb");
        if(!file) {
            fprintf(stderr, "gsr ui: error: ClipboardWin32::set_current_file: failed to open file %s\n", filepath.c_str());
            return;
        }

        fseek(file, 0, SEEK_END);
        const long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);
        if(file_size <= 0) {
            fclose(file);
            return;
        }

        uint8_t *file_data = (uint8_t*)malloc(file_size);
        if(!file_data) {
            fclose(file);
            return;
        }

        const size_t bytes_read = fread(file_data, 1, file_size, file);
        fclose(file);
        if(bytes_read != (size_t)file_size) {
            free(file_data);
            return;
        }

        /* Register the format id (once). PNG is what most consumers
           (browsers, editors) understand; JPG maps to JFIF. */
        static UINT png_format = RegisterClipboardFormatA("PNG");
        static UINT jpg_format = RegisterClipboardFormatA("JFIF");
        const UINT format = file_type == FileType::PNG ? png_format : jpg_format;

        if(OpenClipboard(NULL)) {
            EmptyClipboard();

            HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes_read);
            if(handle) {
                void *dest = GlobalLock(handle);
                if(dest) {
                    memcpy(dest, file_data, bytes_read);
                    GlobalUnlock(handle);
                    SetClipboardData(format, handle);
                } else {
                    GlobalFree(handle);
                }
            }
            CloseClipboard();
        }

        free(file_data);
    }
}
