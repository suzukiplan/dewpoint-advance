#ifndef DEWPOINT_ADVANCE_LOG_TIMESTAMP_H
#define DEWPOINT_ADVANCE_LOG_TIMESTAMP_H

#include <cstddef>
#include <cstdio>
#include <ctime>
#include <streambuf>

namespace DewpointLog
{
constexpr std::size_t TIMESTAMP_BUFFER_SIZE = 21;
constexpr char UNKNOWN_TIMESTAMP[] = "0000.00.00 00:00:00 ";

inline bool formatCurrentTimestamp(char* buffer, std::size_t size)
{
    if (!buffer || size < TIMESTAMP_BUFFER_SIZE) {
        return false;
    }

    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &now) != 0) {
        return false;
    }
#else
    if (!localtime_r(&now, &local)) {
        return false;
    }
#endif
    return std::strftime(buffer, size, "%Y.%m.%d %H:%M:%S ", &local) != 0;
}

inline void writeTimestamp(FILE* file)
{
    if (!file) {
        return;
    }
    char timestamp[TIMESTAMP_BUFFER_SIZE]{};
    if (!formatCurrentTimestamp(timestamp, sizeof(timestamp))) {
        std::fputs(UNKNOWN_TIMESTAMP, file);
        return;
    }
    std::fputs(timestamp, file);
}

class TimestampStreamBuf final : public std::streambuf
{
  private:
    std::streambuf* destination;
    bool enabled;
    bool atLineStart;

    bool writeTimestamp()
    {
        char timestamp[TIMESTAMP_BUFFER_SIZE]{};
        const char* prefix = timestamp;
        if (!formatCurrentTimestamp(timestamp, sizeof(timestamp))) {
            prefix = UNKNOWN_TIMESTAMP;
        }
        const std::streamsize length = static_cast<std::streamsize>(TIMESTAMP_BUFFER_SIZE - 1);
        return destination->sputn(prefix, length) == length;
    }

  protected:
    int_type overflow(int_type value) override
    {
        if (traits_type::eq_int_type(value, traits_type::eof())) {
            return traits_type::not_eof(value);
        }
        if (enabled && atLineStart && !writeTimestamp()) {
            return traits_type::eof();
        }

        const char character = traits_type::to_char_type(value);
        const int_type result = destination->sputc(character);
        if (enabled && !traits_type::eq_int_type(result, traits_type::eof())) {
            atLineStart = character == '\n';
        }
        return result;
    }

    std::streamsize xsputn(const char* data, std::streamsize size) override
    {
        if (!enabled) {
            return destination->sputn(data, size);
        }

        std::streamsize written = 0;
        while (written < size) {
            if (atLineStart && !writeTimestamp()) {
                break;
            }

            const char* begin = data + written;
            const std::streamsize remaining = size - written;
            const char* newline = traits_type::find(begin, remaining, '\n');
            const std::streamsize recordLength = newline ? newline - begin + 1 : remaining;
            const std::streamsize recordWritten = destination->sputn(begin, recordLength);
            written += recordWritten;
            if (recordWritten > 0) {
                atLineStart = data[written - 1] == '\n';
            }
            if (recordWritten != recordLength) {
                break;
            }
        }
        return written;
    }

    int sync() override
    {
        return destination->pubsync();
    }

  public:
    explicit TimestampStreamBuf(std::streambuf* destination)
        : destination(destination), enabled(false), atLineStart(true)
    {
    }

    void enable()
    {
        enabled = true;
        atLineStart = true;
    }
};
} // namespace DewpointLog

#endif
