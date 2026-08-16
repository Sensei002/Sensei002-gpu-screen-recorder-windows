#ifndef MGLPP_CLIPBOARD_HPP
#define MGLPP_CLIPBOARD_HPP

#include <string>

namespace mgl {
    class Clipboard {
    public:
        static void set_string(const std::string &str);
        static std::string get_string();
    };
}

#endif /* MGLPP_CLIPBOARD_HPP */
