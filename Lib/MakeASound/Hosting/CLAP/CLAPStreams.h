#pragma once

#include <clap/stream.h>

#include <string>
#include <string_view>

namespace MakeASound::Hosting
{

// The streams a hosted CLAP plugin saves its state into and loads it from, over
// a string the host owns. Message thread, like clap.state itself; they allocate.
// Internal to the CLAP format: they include the CLAP headers.

// Takes every byte offered, appended to `data`.
class CLAPOutStream
{
public:
    CLAPOutStream();

    CLAPOutStream(const CLAPOutStream&) = delete;
    CLAPOutStream& operator=(const CLAPOutStream&) = delete;

    const clap_ostream* get() const noexcept { return &stream; }

    std::string data;

private:
    clap_ostream stream {};
};

// Hands out `data` from its start, as much as each read asks; 0 at its end. The
// bytes are the caller's and must outlive the stream.
class CLAPInStream
{
public:
    explicit CLAPInStream(std::string_view dataToUse);

    CLAPInStream(const CLAPInStream&) = delete;
    CLAPInStream& operator=(const CLAPInStream&) = delete;

    const clap_istream* get() const noexcept { return &stream; }

private:
    std::string_view data;
    size_t position = 0;
    clap_istream stream {};
};

} // namespace MakeASound::Hosting
