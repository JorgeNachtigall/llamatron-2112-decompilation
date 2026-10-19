/*
 * persist.js (--pre-js) - persistent storage for the web build: IDBFS mounted at /persist and
 * loaded from the browser's IndexedDB before main() runs (run dependency), so settings and
 * scores read at start-up see the saved files. plat_settings_save() calls
 * llamatronPersistSync() to write changes back; Module.onPersistSaved (optional, the shell's
 * test report) is called when a write-back has finished. Nothing leaves the browser.
 */
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
  FS.mkdir('/persist');
  FS.mount(IDBFS, {}, '/persist');
  addRunDependency('llamatron-persist');
  FS.syncfs(true, function (err) {
    if (err) console.warn('persist: could not load saved data', err);
    removeRunDependency('llamatron-persist');
  });
});

var llamatronPersistBusy = false, llamatronPersistAgain = false;
function llamatronPersistSync() {
  if (llamatronPersistBusy) { llamatronPersistAgain = true; return; }
  llamatronPersistBusy = true;
  FS.syncfs(false, function (err) {
    if (err) console.warn('persist: could not save', err);
    else if (Module['onPersistSaved']) Module['onPersistSaved']();
    llamatronPersistBusy = false;
    if (llamatronPersistAgain) { llamatronPersistAgain = false; llamatronPersistSync(); }
  });
}
globalThis.llamatronPersistSync = llamatronPersistSync;
