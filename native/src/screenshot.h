// Sea Trial — headless screenshot capture. Public domain (Unlicense).
//
// A bgfx callback that writes requested screenshots to PNG (via bimg), so the
// game can be run with `--shot <path>` to save a clean frame for scripted
// visual verification / gauntlet-loop critics. Wire callback() into
// bgfx::Init::callback, then bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path).
#pragma once

namespace bgfx { struct CallbackI; }

namespace shot {
bgfx::CallbackI* callback();
}
