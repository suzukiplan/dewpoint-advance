#include <cctype>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace
{

using Definitions = std::map<std::string, std::string>;

void putUsage()
{
    std::cerr << "usage: ./tools/conftype/conftype /path/to/package.conf /path/to/input\n";
}

std::string trim(const std::string& value)
{
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }

    std::size_t last = value.size();
    while (first < last && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

bool readDefinitions(const std::string& path, Definitions& definitions)
{
    std::ifstream config(path);
    if (!config) {
        std::cerr << "conftype: cannot open package configuration: " << path << '\n';
        return false;
    }

    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(config, line)) {
        ++lineNumber;
        if (lineNumber == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            line.erase(0, 3);
        }

        const std::string stripped = trim(line);
        if (stripped.empty() || stripped[0] == '#' || stripped[0] == ';') {
            continue;
        }

        const std::size_t separator = stripped.find('=');
        if (separator == std::string::npos) {
            std::cerr << "conftype: expected 'name = value' at " << path << ':' << lineNumber << '\n';
            return false;
        }

        const std::string name = trim(stripped.substr(0, separator));
        if (name.empty()) {
            std::cerr << "conftype: definition name is empty at " << path << ':' << lineNumber << '\n';
            return false;
        }
        definitions[name] = trim(stripped.substr(separator + 1));
    }

    if (config.bad()) {
        std::cerr << "conftype: failed while reading package configuration: " << path << '\n';
        return false;
    }
    return true;
}

bool readInput(const std::string& path, std::string& input)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        std::cerr << "conftype: cannot open input: " << path << '\n';
        return false;
    }

    input.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    if (stream.bad()) {
        std::cerr << "conftype: failed while reading input: " << path << '\n';
        return false;
    }
    return true;
}

bool writeExpanded(const std::string& input, const Definitions& definitions)
{
    std::size_t position = 0;
    while (position < input.size()) {
        const std::size_t opening = input.find("${", position);
        if (opening == std::string::npos) {
            std::cout.write(input.data() + position, static_cast<std::streamsize>(input.size() - position));
            break;
        }

        std::cout.write(input.data() + position, static_cast<std::streamsize>(opening - position));
        const std::size_t closing = input.find('}', opening + 2);
        if (closing == std::string::npos) {
            std::cout.write(input.data() + opening, static_cast<std::streamsize>(input.size() - opening));
            break;
        }

        const std::string name = input.substr(opening + 2, closing - opening - 2);
        const auto definition = definitions.find(name);
        if (definition == definitions.end()) {
            std::cout.write(input.data() + opening, static_cast<std::streamsize>(closing - opening + 1));
        } else {
            std::cout.write(definition->second.data(), static_cast<std::streamsize>(definition->second.size()));
        }
        position = closing + 1;
    }

    if (!std::cout) {
        std::cerr << "conftype: failed while writing standard output\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 3) {
        putUsage();
        return 1;
    }

    Definitions definitions;
    if (!readDefinitions(argv[1], definitions)) {
        return 1;
    }

    std::string input;
    if (!readInput(argv[2], input)) {
        return 1;
    }
#ifdef _WIN32
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        std::cerr << "conftype: cannot set standard output to binary mode\n";
        return 1;
    }
#endif
    return writeExpanded(input, definitions) ? 0 : 1;
}
