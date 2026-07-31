#include "log_timestamp.h"

#include <cassert>
#include <cstdio>
#include <regex>
#include <sstream>
#include <string>

namespace
{
void testDisabledBufferPassesTextThrough()
{
    std::stringbuf output;
    DewpointLog::TimestampStreamBuf buffer(&output);
    std::ostream stream(&buffer);

    stream << "Before logging\n";

    assert(output.str() == "Before logging\n");
}

void testEnabledBufferPrefixesEveryRecord()
{
    std::stringbuf output;
    DewpointLog::TimestampStreamBuf buffer(&output);
    std::ostream stream(&buffer);
    buffer.enable();

    stream << "First";
    stream << " record\nSecond record\n\n";

    const std::regex expected(
        R"(^\d{4}\.\d{2}\.\d{2} \d{2}:\d{2}:\d{2} First record\n)"
        R"(\d{4}\.\d{2}\.\d{2} \d{2}:\d{2}:\d{2} Second record\n)"
        R"(\d{4}\.\d{2}\.\d{2} \d{2}:\d{2}:\d{2} \n$)");
    assert(std::regex_match(output.str(), expected));
}

void testEnableStartsANewTimestampedRecord()
{
    std::stringbuf output;
    DewpointLog::TimestampStreamBuf buffer(&output);
    std::ostream stream(&buffer);

    stream << "Console output without newline";
    buffer.enable();
    stream << "File record\n";

    const std::regex expected(
        R"(^Console output without newline)"
        R"(\d{4}\.\d{2}\.\d{2} \d{2}:\d{2}:\d{2} File record\n$)");
    assert(std::regex_match(output.str(), expected));
}

void testFileTimestampUsesRequiredFormat()
{
    FILE* file = std::tmpfile();
    assert(file);

    DewpointLog::writeTimestamp(file);
    std::rewind(file);
    char timestamp[DewpointLog::TIMESTAMP_BUFFER_SIZE]{};
    assert(std::fread(timestamp, 1, sizeof(timestamp) - 1, file) == sizeof(timestamp) - 1);
    std::fclose(file);

    const std::regex expected(R"(^\d{4}\.\d{2}\.\d{2} \d{2}:\d{2}:\d{2} $)");
    assert(std::regex_match(timestamp, expected));
}
} // namespace

int main()
{
    testDisabledBufferPassesTextThrough();
    testEnabledBufferPrefixesEveryRecord();
    testEnableStartsANewTimestampedRecord();
    testFileTimestampUsesRequiredFormat();
    return 0;
}
