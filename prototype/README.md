# Sea Trial Prototype — Web Implementation

A single-file React + Three.js prototype of the vertical slice. Implements the slice's core loop in the browser as a real 3D scene: build a ship → validate → save → load in Sail → float on Gerstner waves → cargo / damage → sink or recover.

This is **not** the engine-based prototype the slice plan ultimately calls for ([../vertical-slice.md §3](../vertical-slice.md) leans Unreal). It's a faster proof — runnable in any browser without a build step — to validate the math, the spine, and the 3D feel before sinking time into an engine project.

## Files

- `App.jsx` — canonical React component (ES module). Drop into a Vite / Next.js project. Imports `three`.
- `index.html` — self-contained runnable demo. Loads React 18, ReactDOM, Three.js r149, Babel standalone, and Tailwind via CDN. Same component code inlined.
- `README.md` — this file.

Keep `App.jsx` and `index.html` in sync if you edit either.

## Run it

Fastest path: open `index.html` in any modern browser.

Some browsers block scripts from `file://` URLs. If `index.html` shows a blank page, serve it locally:

```
python -m http.server 8080
# then visit http://localhost:8080/index.html
```

## What it implements

Mapped to [../vertical-slice.md](../vertical-slice.md):

- Main menu (§5)
- Build Test scene (§6) — generated pieces, editable hull dims, materials, systems toggles, live 3D dry-dock preview
- Sail Test scene (§7) — keyboard (W/S/A/D, C/V/X/Z/R/L/F1) + on-screen buttons, 3D follow camera
- Ship file format (§11) — versioned JSON, localStorage save/load
- Validation (§12) — errors + warnings + stats
- Deterministic multi-frequency Gerstner waves (§8) — animated 3D mesh ocean
- Tier 2 (3×3) buoyancy sampling (§9, §10) — sample markers visible as yellow spheres in 3D
- Damage + cargo + sinking (§13) — pieces visibly darken/destroy in 3D
- Debug overlay (§16) — runtime stats panel + 3D sample markers

## What's deliberately not here

- Networking — single-player only (slice §14).
- Multiplayer determinism scaffolding is in place (seeded waves, serializable state) but no actual sync.
- Engine integration — this is a browser prototype, not the Unreal target.
- Real board bending, real sailing aerodynamics, real Workshop, real Steam Cloud (all in slice §20 "What to fake").

## Source

Generated as a ChatGPT artifact. `App.jsx` is the React component verbatim; `index.html` wraps the same code with React + Three.js + Tailwind + Babel CDN bootstrap. Heights on the Three.js host divs are inline-styled to avoid Tailwind JIT-class load-order races on initial mount.
