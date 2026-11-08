#pragma once

// Notices when the GUI thread stops responding and records where it is stuck,
// so an unreproducible freeze still leaves evidence behind.
//
// A background thread posts a no-op to the GUI thread once a second. If one goes
// unanswered for kStallSeconds, the GUI thread's call stack is appended to
// hang.log in the application data folder (module+offset per frame: resolve them
// with llvm-symbolizer or nm against the same executable), followed by how long
// the stall lasted if it ever ends. It only observes: nothing is killed or
// restarted. Modal dialogs don't count, since they keep processing events.
namespace HangWatchdog
{
void start(); // call on the GUI thread, once QApplication exists
void stop();  // call on the GUI thread, before QApplication goes away
} // namespace HangWatchdog
