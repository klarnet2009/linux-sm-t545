`drm_hwcomposer-flattening-deadlock.diff` is a `git diff` for `external/drm_hwcomposer` (AOSP 25Q2 / LineageOS 23): `FlatteningController::ThreadFn` called the refresh
callback with its mutex held while a binder thread held the composer client lock and called `NewFrame()`: an AB/BA deadlock that froze the display for good.
The callback now runs unlocked, and the timer uses `steady_clock`. Licence: Apache-2.0 like drm_hwcomposer.
