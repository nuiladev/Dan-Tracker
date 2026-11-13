# Dan Tracker

osu!mania overlay for dans. Shows accuracy and judgements for each song in the dan, not just the whole map, plus the running total. Uses [tosu](https://github.com/tosuapp/tosu) for the live data.

## How to use
1. Open tosu and osu! (windowed or borderless, exclusive fullscreen hides overlays).
2. Run `DanTracker.exe` with `dans.json` next to it.
3. Drag it where you want and press Ctrl+Shift+L to lock it.

Shortcuts (all Ctrl+Shift):
- H: hide / show (still shows in OBS)
- L: lock / unlock
- R: reload `dans.json`
- D: mark / unmark current map as a dan
- G: green screen window for OBS
- + / -: size
- Q: quit

Right-click or the tray icon for the menu.

## OBS
- Window Capture: `[DanTracker.exe]: Dan Tracker`, method "Windows 10 (1903 and up)".
- Browser Source: `http://127.0.0.1:24051/` (add `?bg=0.4` or `?bg=0` for less background).
- Or Ctrl+Shift+G for the green window + Chroma Key.

Hiding the overlay moves it off-screen instead of minimizing, so OBS keeps capturing it.

## Config
Everything is in `dans.json`. Mania maps with one of the `autoKeywords` in the title/diff/tags are detected as dans. Songs are split by finding the long pauses between them (`minGapMs`). If that doesn't work for a map, add it under `dans` with `titleContains` / `versionContains` / `checksum` and set `splitsMs` by hand, e.g. `[185000, 372000, 560000]`.

## Build
Run `build.bat` (needs Visual Studio C++). It builds, runs the tests and puts the exe in `build\`.

## Credits
- NuilaDev (Discord: zrwz0): made it
- eQualize (Discord: frawog): testing

Thanks to the tosu devs, this wouldn't work without it.

## License
MIT
