# Input Scenario Runner

This project supports scripted input playback for repeatable interaction tests and profiling.

The runner reuses existing `Application` input handlers and is enabled by environment variables.

## Why

- Reproduce pan/zoom behavior without manual interaction.
- Generate comparable profiler logs across runs.
- Let automation agents run interaction scenarios end-to-end.

## Enable

Set `GRAPHX_INPUT_SCRIPT` before launching `GraphXplorer.exe`.

If set, app startup prints:

`[InputScenarioRunner] Scripted input enabled via GRAPHX_INPUT_SCRIPT.`

## Script Grammar

Script is a semicolon-separated action list:

`action;action;action`

Supported actions:

- `drag(dx,dy,frames)`
- `scroll(offset,frames)`
- `resize(width,height,frames)`
- `key(name,press|release[,frames])`
- `formula(expression)`
- `text(characters)`
- `click(x,y[,frames])`
- `capture(path[,frames])`
- `pause(frames)`

Rules:

- `frames` must be a positive integer.
- `drag`/`scroll` values are doubles.
- `capture` saves the current OpenGL backbuffer to a PNG after that frame renders and before buffer swap.
- `text` sends characters through the same formula-input text path as keyboard entry. Use `key(I,release)` first to open the editor.
- `click` sends a pixel-coordinate click through the same UI hit-test path as the mouse.
- Whitespace is allowed.

Example:

`drag(6,0,120);drag(0,6,120);scroll(1,24);scroll(-1,24);pause(10)`

## Environment Variables

- `GRAPHX_INPUT_SCRIPT` (required): scripted sequence.
- `GRAPHX_INPUT_LOOP` (optional, default `0`): loop script forever when `1`.
- `GRAPHX_INPUT_EXIT_ON_COMPLETE` (optional, default `0`): close app when script finishes when `1`.
- `GRAPHX_INPUT_WAIT_MS` (optional, default `8`): event wait timeout in milliseconds while script is active.

## Profiling Example (PowerShell)

```powershell
Remove-Item graphx_profile.log -ErrorAction SilentlyContinue
$env:GRAPHX_PROFILE='1'
$env:GRAPHX_INPUT_SCRIPT='drag(6,0,120);drag(0,6,120);scroll(1,24);scroll(-1,24);pause(10)'
$env:GRAPHX_INPUT_EXIT_ON_COMPLETE='1'
$env:GRAPHX_INPUT_WAIT_MS='8'
& D:\GraphXplorer\out\build\x64-Release\GraphXplorer.exe
rg -n "main.scriptedInput|plot.onCursorDrag|scene.onPlotRangeChanged" D:\GraphXplorer\graphx_profile.log
```

## Debug Capture Example (PowerShell)

```powershell
$env:GRAPHX_INPUT_SCRIPT='key(D,press);pause(30);capture(D:\GraphXplorer\.codex_debug_frame.png);pause(1)'
$env:GRAPHX_INPUT_EXIT_ON_COMPLETE='1'
& D:\GraphXplorer\build\GraphXplorer.exe
```

Formula capture:

```powershell
$env:GRAPHX_INPUT_SCRIPT='formula(x^2+y^2<25);key(D,press);pause(45);capture(D:\GraphXplorer\.codex_circle.png);pause(1)'
$env:GRAPHX_INPUT_EXIT_ON_COMPLETE='1'
& D:\GraphXplorer\build\GraphXplorer.exe
```

Formula-editor capture:

```powershell
$env:GRAPHX_INPUT_SCRIPT='resize(360,640,1);key(I,release);text(+sin(x));key(LEFT,press);key(DELETE,press);pause(4);capture(D:\GraphXplorer\.codex_ui_input_active.png);pause(1)'
$env:GRAPHX_INPUT_EXIT_ON_COMPLETE='1'
& D:\GraphXplorer\build\GraphXplorer.exe
```

## Agent Quick Start

Use this when you need reproducible drag/zoom profiling:

1. Set `GRAPHX_PROFILE=1`.
2. Set a short `GRAPHX_INPUT_SCRIPT`.
3. Set `GRAPHX_INPUT_EXIT_ON_COMPLETE=1` so run ends automatically.
4. Launch release executable.
5. Parse `graphx_profile.log`.

## Troubleshooting

- If parsing fails, script is ignored and app runs normally.
- If app does not exit, verify `GRAPHX_INPUT_EXIT_ON_COMPLETE=1`.
- If no profile file appears, verify `GRAPHX_PROFILE=1`.
