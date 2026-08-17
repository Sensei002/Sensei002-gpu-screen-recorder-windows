#pragma once

#include <string>

#include "Clipboard.hpp"

namespace gsr {
    // Windows equivalent of ClipboardX11: places a screenshot image file on
    // the Windows clipboard. The file bytes are copied in (owned), so the
    // clipboard survives the file being moved/deleted — matching X11's
    // INCR-transfer semantics in effect. PNG is advertised via the
    // "PNG" format id; JPG via "JFIF" (image/jpeg on Windows is not a
    // standard clipboard format, so we use the widely-consumed ids).
    class ClipboardWin32 : public Clipboard {
    public:
        ClipboardWin32();
        ~ClipboardWin32() override;
        ClipboardWin32(const ClipboardWin32&) = delete;
        ClipboardWin32& operator=(const ClipboardWin32&) = delete;

        void set_current_file(const std::string &filepath, FileType file_type) override;
    };
}
