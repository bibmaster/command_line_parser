#pragma once
// Copyright (c) 2023 Dmitry Sokolov
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Tiny command line parser.

#include <charconv>
#include <optional>
#include <string>
#include <vector>

using namespace std::literals;

namespace univang {

class CommandLineParser {
    using ParseFn = bool (*)(void*, std::string_view);
    enum class OptionType : uint8_t { param, flag, list };
    struct Option {
        OptionType type;
        bool parsed = false;
        bool required = false;
        void* value;
        ParseFn parse;
        std::string_view name;
        std::string_view flags;
        std::string_view help;
        std::string_view hint;
        int position;
        Option(
            OptionType type, void* value, ParseFn parse, std::string_view spec,
            std::string_view help = {}, int position = 0);
    };

    static bool parse(std::string_view str, std::string_view& value) {
        value = str;
        return true;
    }
    static bool parse(std::string_view str, std::string& value) {
        value = str;
        return true;
    }
    template<class T>
    static std::enable_if_t<std::is_arithmetic_v<T>, bool> parse(
        std::string_view str, T& value) {
        auto res = std::from_chars(str.data(), str.data() + str.size(), value);
        return res.ec == std::errc{} && res.ptr == str.data() + str.size();
    }
    template<class T>
    static bool parse(std::string_view str, std::optional<T>& value) {
        return parse(str, value.emplace());
    }
    static bool parseFlag(void* value, std::string_view /*str*/) {
        *static_cast<bool*>(value) = true;
        return true;
    }
    template<class T>
    static bool parseValue(void* value, std::string_view str) {
        return parse(str, *static_cast<T*>(value));
    }
    template<class T, class Alloc>
    static bool parseList(void* value, std::string_view str) {
        auto& list = *static_cast<std::vector<T, Alloc>*>(value);
        return parse(str, list.emplace_back());
    }

public:
    CommandLineParser& addFlag(
        bool& value, std::string_view spec, std::string_view help = {}) {
        options_.emplace_back(OptionType::flag, &value, &parseFlag, spec, help);
        return *this;
    }
    template<class T>
    CommandLineParser& add(
        T& value, std::string_view spec, std::string_view help = {},
        int position = 0) {
        options_.emplace_back(
            OptionType::param, &value, &parseValue<T>, spec, help, position);
        return *this;
    }
    template<class T, class Alloc>
    CommandLineParser& add(
        std::vector<T, Alloc>& value, std::string_view spec,
        std::string_view help = {}, int position = 0) {
        options_.emplace_back(
            OptionType::list, &value, &parseList<T, Alloc>, spec, help,
            position);
        return *this;
    }

    CommandLineParser& setProgram(std::string_view name) {
        program_ = name;
        return *this;
    }
    CommandLineParser& skipUnknown(bool value = true) {
        skipUnknown_ = value;
        return *this;
    }
    bool skipUnknown() const {
        return skipUnknown_;
    }
    const std::string& error() const {
        return error_;
    }

    bool checkRequired();
    bool parse(int argc, char** argv);

    std::string getHelp() const;

private:
    Option* findOption(int position);
    Option* findOption(char optChar);
    Option* findOption(std::string_view name);
    bool parseOption(Option& opt, std::string_view value);
    static size_t formatOptName(const Option& opt, std::string& result);
    void formatArgError(std::string_view msg, std::string_view arg) {
        error_ += msg;
        error_ += ": "sv;
        error_ += arg;
    }

private:
    std::string_view program_;
    bool skipUnknown_ = false;
    std::vector<Option> options_;
    std::string error_;
};

inline CommandLineParser::Option::Option(
    OptionType type, void* value, ParseFn parse, std::string_view spec,
    std::string_view help, int position)
    : type(type), value(value), parse(parse), help(help), position(position) {
    required = !spec.empty() && spec[0] == '+';
    if(required)
        spec.remove_prefix(1);
    name = spec;
    auto pos = spec.find(',');
    if(pos == std::string_view::npos)
        return;
    name = spec.substr(0, pos);
    spec.remove_prefix(pos + 1);
    flags = spec;
    pos = spec.find(',');
    if(pos == std::string_view::npos)
        return;
    flags = spec.substr(0, pos);
    hint = spec.substr(pos + 1);
}

inline CommandLineParser::Option* CommandLineParser::findOption(int position) {
    Option* positionalOpt = nullptr;
    for(auto& opt : options_) {
        if(opt.position == position)
            return &opt;
        if(opt.position == -1)
            positionalOpt = &opt;
    }
    return positionalOpt;
}

inline CommandLineParser::Option* CommandLineParser::findOption(char optChar) {
    for(auto& opt : options_) {
        if(opt.flags.find(optChar) != std::string_view::npos)
            return &opt;
    }
    if(!skipUnknown_) {
        error_ = "unknown option: -"sv;
        error_ += optChar;
    }
    return nullptr;
}

CommandLineParser::Option* CommandLineParser::findOption(
    std::string_view name) {
    for(auto& opt : options_) {
        if(opt.name == name)
            return &opt;
    }
    if(!skipUnknown_) {
        error_ = "unknown option: --"sv;
        error_ += name;
    }
    return nullptr;
}

bool CommandLineParser::parseOption(Option& opt, std::string_view value) {
    if(opt.parse(opt.value, value))
        return true;
    formatArgError("invalid option value"sv, value);
    return false;
}

bool CommandLineParser::parse(int argc, char** argv) {
    program_ = argv[0];
    size_t pathSepPos = program_.find_last_of("/\\"sv);
    if(pathSepPos != std::string_view::npos)
        program_ = program_.substr(pathSepPos + 1);
    bool hasPosArg = false;
    for(auto& opt : options_) {
        if(!opt.position)
            continue;
        hasPosArg = true;
        break;
    }
    int argNum = 1;
    int position = 0;
    Option* lastOption = nullptr;
    bool lastOptionUnknown = false;
    while(argNum < argc) {
        std::string_view argValue(argv[argNum++]);
        auto arg = argValue;
        if(arg.empty())
            continue;
        if(arg[0] != '-') {
            if(lastOptionUnknown) {
                lastOptionUnknown = false;
                continue;
            }
            if(lastOption) {
                lastOption->parsed = true;
                if(!parseOption(*lastOption, arg))
                    return false;
                if(lastOption->type != OptionType::list || hasPosArg)
                    lastOption = nullptr;
            }
            else {
                ++position;
                auto* option = findOption(position);
                if(!option) {
                    formatArgError("positional arg not allowed"sv, argValue);
                    return false;
                }
                option->parsed = true;
                if(!parseOption(*option, arg))
                    return false;
            }
            continue;
        }
        lastOption = nullptr;
        lastOptionUnknown = false;
        arg.remove_prefix(1);
        bool isName = !arg.empty() && arg[0] == '-';
        if(isName)
            arg.remove_prefix(1);
        if(arg.empty())
            continue;
        std::string_view name = arg;
        std::string_view value;
        auto eqPos = arg.find('=');
        if(eqPos == 0) {
            formatArgError("missing option name"sv, argValue);
            return false;
        }
        bool hasValue = eqPos != std::string_view::npos;
        if(hasValue) {
            name = arg.substr(0, eqPos);
            value = arg.substr(eqPos + 1);
        }
        Option* option = nullptr;
        if(isName)
            option = findOption(name);
        else if(name.size() == 1)
            option = findOption(name[0]);
        else {
            if(hasValue) {
                formatArgError("flag/argument mix disallowed"sv, argValue);
                return false;
            }
            for(auto optChar : name) {
                option = findOption(optChar);
                if(!option) {
                    if(skipUnknown_)
                        continue;
                    return false;
                }
                if(option->type != OptionType::flag) {
                    error_ = "option requires value: "sv;
                    error_ += optChar;
                    return false;
                }
                option->parsed = true;
                option->parse(option->value, {});
            }
            continue;
        }
        if(!option) {
            if(skipUnknown_) {
                lastOptionUnknown = true;
                continue;
            }
            return false;
        }
        if(option->type == OptionType::flag) {
            if(hasValue) {
                formatArgError("option value unexpected"sv, argValue);
                return false;
            }
            option->parsed = true;
            option->parse(option->value, {});
            continue;
        }
        if(!hasValue) {
            lastOption = option;
            continue;
        }
        option->parsed = true;
        if(!parseOption(*option, value))
            return false;
        if(option->type == OptionType::list && !hasPosArg)
            lastOption = option;
    }
    if(!lastOption || lastOption->parsed)
        return true;
    formatArgError("option requires value"sv, argv[argc - 1]);
    return false;
}

bool CommandLineParser::checkRequired() {
    for(auto& opt : options_) {
        if(!opt.required || opt.parsed)
            continue;
        error_ = "required option missing: "sv;
        formatOptName(opt, error_);
        return false;
    }
    return true;
}

inline size_t CommandLineParser::formatOptName(
    const Option& opt, std::string& result) {
    size_t sz = result.size();
    if(opt.name.empty() && opt.flags.empty()) {
        if(!opt.hint.empty())
            result += opt.hint;
        else {
            result += "arg"sv;
            result += std::to_string(opt.position);
        }
        return result.size() - sz;
    }
    if(!opt.flags.empty()) {
        result += '-';
        result += opt.flags;
    }
    if(!opt.name.empty()) {
        if(!opt.flags.empty())
            result += " [ "sv;
        result += "--"sv;
        result += opt.name;
        if(!opt.flags.empty())
            result += " ]"sv;
    }
    if(opt.type != OptionType::flag) {
        result += ' ';
        if(opt.hint.empty())
            result += "arg"sv;
        else
            result += opt.hint;
    }
    return result.size() - sz;
}

inline std::string CommandLineParser::getHelp() const {
    std::string result;
    result = "usage: "sv;
    result += program_;
    result += " [options]"sv;
    bool hasOptions = false;
    bool hasPositionalArgs = false;
    std::string namebuf;
    size_t maxNameLen = 0;
    size_t maxArgLen = 0;
    for(auto& opt : options_) {
        namebuf.clear();
        auto nameLen = formatOptName(opt, namebuf);
        if(!opt.name.empty() || !opt.flags.empty()) {
            if(namebuf.size() > maxNameLen)
                maxNameLen = namebuf.size();
            hasOptions = true;
        }
        else {
            if(nameLen > maxArgLen)
                maxArgLen = nameLen;
            hasPositionalArgs = true;
            result += ' ';
            if(!opt.required)
                result += '[';
            result += namebuf;
            if(opt.type == OptionType::list)
                result += "..."sv;
            if(!opt.required)
                result += ']';
        }
    }
    result += '\n';
    if(maxNameLen > 30)
        maxNameLen = 30;
    if(maxArgLen > 30)
        maxArgLen = 30;
    if(hasOptions) {
        result += "allowed options:\n"sv;
        for(auto& opt : options_) {
            if(opt.name.empty() && opt.flags.empty())
                continue;
            result += "  "sv;
            auto nameLen = formatOptName(opt, result);
            if(nameLen < maxNameLen)
                result.append(maxNameLen - nameLen, ' ');
            if(!opt.help.empty()) {
                result += " : "sv;
                result += opt.help;
            }
            result += '\n';
        }
    }
    if(hasPositionalArgs) {
        result += "positional arguments:\n"sv;
        for(auto& opt : options_) {
            if(!opt.name.empty() || !opt.flags.empty())
                continue;
            result += "  "sv;
            auto nameLen = formatOptName(opt, result);
            if(nameLen < maxArgLen)
                result.append(maxArgLen - nameLen, ' ');
            if(!opt.help.empty()) {
                result += " : "sv;
                result += opt.help;
            }
            result += '\n';
        }
    }
    return result;
}

} // namespace univang
