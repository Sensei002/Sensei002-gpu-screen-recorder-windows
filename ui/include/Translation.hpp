#pragma once

#include <string>
#include <unordered_map>

namespace gsr {
    class Translation {
    public:
        static Translation& instance();
        void init(const char* translations_directory, const char* initial_language = nullptr);
        bool load_language(std::string_view lang);
        bool is_language_supported(std::string_view lang);
        bool plural_numbers_are_complex();
        const char* translate(const char* key);

        std::string get_complex_plural_number_key(const char* key, int number);

        template<typename... Args>
        std::string format(const char* key, Args&&... args) {
            const char* fmt = translate(key);

            // result buffer
            char buffer[4096];
            snprintf(buffer, sizeof(buffer), fmt, std::forward<Args>(args)...);
            return std::string(buffer);
        }

    private:
        std::string get_system_language();
        std::string trim(const std::string& str);
        void process_escapes(std::string& str);

    private:
        std::string translations_directory;
        std::string current_language = "en";
        std::unordered_map<std::string, std::string> translations;
    };
}

#define TR(s) gsr::Translation::instance().translate(s)
#define TRF(s, ...) gsr::Translation::instance().format(s, __VA_ARGS__)
#define TRC(s, n) gsr::Translation::instance().get_complex_plural_number_key(s, n)
#define TRPF(s, n, ...) TRF(TRC(s, n).c_str(), __VA_ARGS__)