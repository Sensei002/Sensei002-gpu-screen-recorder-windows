#include "../include/Translation.hpp"
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <fstream>


namespace gsr {
    static void string_replace_characters(char *str, const char *characters_to_replace, char new_character) {
        for(; *str != '\0'; ++str) {
            for(const char *p = characters_to_replace; *p != '\0'; ++p) {
                if(*str == *p)
                    *str = new_character;
            }
        }
    }

    std::string Translation::get_system_language() {
        const char* env_vars[] = { "LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG" };
        for (const char* env_var : env_vars) {
            const char* lang = getenv(env_var);
            if (!lang || !lang[0])
                continue;

            std::string lang_str(lang);

            // Ignore non-language locales such as C/POSIX and fall back to the next variable
            if (lang_str == "C" || lang_str == "C.UTF-8" || lang_str == "POSIX")
                continue;

            string_replace_characters(lang_str.data(), "-", '_');

            size_t dot = lang_str.find('.');
            if (dot != std::string::npos) {
                return lang_str.substr(0, dot);
            }
            return lang_str;
        }

        return "en";
    }

    void Translation::process_escapes(std::string& str) {
        size_t pos = 0;
        while ((pos = str.find("\\n", pos)) != std::string::npos) {
            str.replace(pos, 2, "\n");
            pos += 1;
        }
    }

    bool Translation::is_language_supported(std::string_view lang) {
        if(lang == "en")
            return true;

        std::string paths[] = {
            std::string("translations/") + std::string(lang) + ".txt",
            std::string(this->translations_directory) + std::string(lang) + ".txt"
        };

        for (const auto& path : paths) {
            std::ifstream file(path);
            if (file.is_open()) {
                return true;
            }
        }

        return false;
    }

    bool Translation::load_language(std::string_view lang) {
        translations.clear();

        const std::string system_language = get_system_language();
        if(lang.empty())
            lang = system_language;

        if (!is_language_supported(lang)) {
            // supports two symbols locale translation
            size_t underscore = lang.find('_');
            if (underscore != std::string::npos) {
                lang = lang.substr(0, underscore);
            }

            if (!is_language_supported(lang)) {
                fprintf(stderr, "Warning: language '%.*s' is not supported\n", (int)lang.size(), lang.data());
                return false;
            }
        }

        current_language = lang;

        if (lang == "en") {
            return true; // english is the base
        }

        std::string paths[] = {
            std::string("translations/") + std::string(lang) + ".txt",
            std::string(this->translations_directory) + std::string(lang) + ".txt"
        };

        for (const auto& path : paths) {
            std::ifstream file(path);
            if (!file.is_open()) continue;

            std::string line;
            while (std::getline(file, line)) {
                if (line.empty() || line[0] == '#') continue;

                size_t eq_pos = line.find('=');
                if (eq_pos == std::string::npos) continue;

                std::string key = line.substr(0, eq_pos);
                std::string value = line.substr(eq_pos + 1);

                // Process escape sequences in both key and value
                process_escapes(key);
                process_escapes(value);

                translations[key] = value;
            }

            fprintf(stderr, "Info: Loaded translation file for '%.*s' from %s\n", (int)lang.size(), lang.data(), path.c_str());
            return true;
        }

        fprintf(stderr, "Warning: translation file for '%.*s' not found\n", (int)lang.size(), lang.data());
        return false;
    }

    Translation& Translation::instance() {
        static Translation tr;
        return tr;
    }

    void Translation::init(const char* translations_directory, const char* initial_language) {
        if(initial_language && initial_language[0] == '\0')
            initial_language = nullptr;

        this->translations_directory = translations_directory;

        load_language(initial_language == nullptr ? "" : initial_language);
    }

    bool Translation::plural_numbers_are_complex() {
        if (
            current_language == "ru" ||
            current_language == "uk" ||
            current_language == "pl" ||
            current_language == "cs" ||
            current_language == "sk" ||
            current_language == "hr" ||
            current_language == "sl"
        ) {
            return true;
        }

        return false;
        //return current_language != "en";
    }

    std::string Translation::get_complex_plural_number_key(const char* key, int number) {
        std::string s_key = key;
        if (current_language == "ru" || current_language == "uk") {
            int n = number % 100;
            if (n >= 11 && n <= 14) {
                return s_key + "_many";
            }
            n = number % 10;
            if (n == 1) {
                return s_key + "_one";
            }
            if (n >= 2 && n <= 4) {
                return s_key + "_few";
            }
            return s_key + "_many";
        } else if (current_language == "pl") {
            int n = number % 100;
            if (n >= 12 && n <= 14) {
                return s_key + "_many";
            }
            n = number % 10;
            if (n == 1) {
                return s_key + "_one";
            }
            if (n >= 2 && n <= 4) {
                return s_key + "_few";
            }
            return s_key + "_many";
        }
        // Add more languages as needed

        return key; // default fallback
    }

    const char* Translation::translate(const char* key) {
        auto it = translations.find(key);
        if (it != translations.end()) {
            return it->second.c_str();
        }
#ifndef DNDEBUG
        if(current_language != "en")
            fprintf(stderr, "Warning: translation key '%s' not found\n", key);
#endif
        return key; // falling back if nothing found
    }
}
