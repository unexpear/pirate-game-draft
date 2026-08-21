// Sea Trial — headless screenshot capture. Public domain (Unlicense).
#include "screenshot.h"

#include <bgfx/bgfx.h>
#include <bimg/bimg.h>
#include <bx/error.h>
#include <bx/file.h>
#include <bx/filepath.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace {

// Minimal bgfx callback: everything is a stub except screenShot, which encodes
// the BGRA backbuffer to a PNG through bimg.
struct ShotCallback : public bgfx::CallbackI {
    virtual ~ShotCallback() {}

    void fatal(const char* /*_filePath*/, uint16_t /*_line*/, bgfx::Fatal::Enum /*_code*/, const char* _str) override {
        std::fprintf(stderr, "bgfx FATAL: %s\n", _str);
        std::abort();
    }
    void traceVargs(const char* /*_filePath*/, uint16_t /*_line*/, const char* /*_format*/, va_list /*_argList*/) override {}
    void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
    void cacheWrite(uint64_t, const void*, uint32_t) override {}

    void screenShot(const char* _filePath, uint32_t _width, uint32_t _height, uint32_t _pitch,
                    bgfx::TextureFormat::Enum /*_format*/, const void* _data, uint32_t /*_size*/,
                    bool _yflip) override {
        bx::FileWriter writer;
        bx::Error err;
        if (writer.open(bx::FilePath(_filePath), false, &err)) {
            // Screenshot data is documented as always 4-byte BGRA.
            bimg::imageWritePng(&writer, _width, _height, _pitch, _data, bimg::TextureFormat::BGRA8, _yflip, &err);
            writer.close();
            std::printf("shot: wrote %s (%ux%u)\n", _filePath, _width, _height);
            std::fflush(stdout);
        } else {
            std::fprintf(stderr, "shot: could not open %s for writing\n", _filePath);
        }
    }

    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, uint32_t) override {}
};

ShotCallback s_cb;

} // namespace

namespace shot {
bgfx::CallbackI* callback() { return &s_cb; }
}
