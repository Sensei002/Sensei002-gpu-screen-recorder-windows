#!/usr/bin/env python3

import sys
import os
import json

# Skips steam games and games that end with .x86_64, .x86 or .x64
def extract_non_tracked_games(filepath):
    game_names = []
    with open(filepath, "r") as file:
        is_steam_game = False
        for line in file:
            if line.startswith("#"):
                if "steam" in line:
                    is_steam_game = True
            elif line.startswith("{"):
                if is_steam_game:
                    continue

                app_info = json.loads(line)
                if app_info["type"] != "Game":
                    continue

                game_name = app_info["name"]
                if not game_name.endswith(".x86_64") and not game_name.endswith(".x86") and not game_name.endswith(".x64"):
                    game_names.append(json.loads(line)["name"])
            else:
                is_steam_game = False
    return game_names

def write_process_name_matcher_code_file(filepath, all_games):
    games_by_len = {}
    for game in all_games:
        l = games_by_len.get(len(game), [])
        l.append(game)
        games_by_len[len(game)] = l

    games_by_len = dict(sorted(games_by_len.items()))

    with open(filepath, "w") as file:
        file.write(
            '#include "native_games.h"\n'
            '\n'
            '#include <string.h>\n'
            '\n'
            '/* This file was auto-generated with create-native-games-list.py, do not edit manually! */\n'
            '\n')

        for game_len in games_by_len:
            games_quoted = ", ".join('"%s"' % game for game in games_by_len[game_len])
            file.write("static const char *process_names_len_%d[] = { %s, NULL };\n" % (game_len, games_quoted))

        file.write("\n")
        file.write(
            "bool is_process_name_native_game(const char *process_name, size_t size) {\n"
            "    switch(size) {\n")

        for game_len in games_by_len:
            file.write(
                "        case %d: {\n"
                "            for(size_t i = 0; process_names_len_%d[i] != NULL; ++i) {\n"
                "                if(memcmp(process_name, process_names_len_%d[i], %d) == 0)\n"
                "                    return true;\n"
                "            }\n"
                "            return false;\n"
                "        }\n" % (game_len, game_len, game_len, game_len))

        file.write(
            "        default: {\n"
            "            return false;\n"
            "        }\n"
            "    }\n"
            "}\n"
        )

def add_games_from_rules_filepath(filepath):
    games = extract_non_tracked_games(filepath)
    print("Scanning file: %s, num games found: %d" % (filepath, len(games)))
    return games

def add_games_from_rules_recursive(dir):
    games_added = []
    for folder, subfolders, files in os.walk(dir):
        for file in files:
            filepath = os.path.abspath(os.path.join(folder, file))
            games_added.extend(add_games_from_rules_filepath(filepath))
    return games_added

def main():
    if len(sys.argv) != 2:
        print("usage: create-native-games-list.py <path-to-ananicy-rules>")
        exit(1)

    ananicy_root_path = sys.argv[1]
    ananicy_conf_path = os.path.join(ananicy_root_path, "ananicy.conf")
    if not os.path.exists(ananicy_conf_path):
        print("error: the path this script was launched with is not the ananicy-rules root directory (file %s doesn't exist)" % ananicy_conf_path)
        exit(1)

    ananicy_native_games_path = os.path.join(ananicy_root_path, "00-default", "Games", "linux-native")
    ananicy_emulators_path = os.path.join(ananicy_root_path, "00-default", "Games", "emulators.rules")
    ananicy_launchers_path = os.path.join(ananicy_root_path, "00-default", "Games", "launchers.rules")
    ananicy_waydroid_path = os.path.join(ananicy_root_path, "00-default", "Games", "waydroid.rules")

    all_games = []
    all_games.extend(add_games_from_rules_recursive(ananicy_native_games_path))
    all_games.extend(add_games_from_rules_filepath(ananicy_emulators_path))
    all_games.extend(add_games_from_rules_filepath(ananicy_launchers_path))
    all_games.extend(add_games_from_rules_filepath(ananicy_waydroid_path))
    all_games.append("supertux2")
    all_games.append("etr") # extreme tux racer
    all_games.append("TETR.IO")
    # game streaming clients
    all_games.append("parsecd")
    all_games.append("moonlight")
    all_games.append("moonlight-qt")

    blacklisted_games = ("lsfg-vk-ui ")
    all_games = filter(lambda game: game not in blacklisted_games, all_games)

    script_dir = os.path.dirname(os.path.realpath(__file__))
    write_process_name_matcher_code_file(os.path.join(script_dir, "native_games.c"), all_games)

main()
