// sfxr_break.h - break switches: deliberately break one named behavior so a
// test can be SEEN failing (docs/TESTING.md, "Break switches").
//
//   if (!SFXR_BREAK(vrui_button_side_entry)) armed = armed && above;
//
// Only test builds (make test, -DSFXR_TESTING) contain them: there, a switch
// is on when its name is listed in SFXR_BREAK=name[,name...]. In every other
// build SFXR_BREAK(x) is the constant 0 -- no code, no strings.
//
// Every switch is declared once, with one sentence, in its module's
// *_breaks.def (sfxr/src/sfxr_breaks.def, vrui/src/vrui_breaks.def, and your
// app's own). A name that isn't declared there is a compile error in test
// builds, so a test can't name a switch that doesn't exist.

#ifndef SFXR_BREAK_H
#define SFXR_BREAK_H

#include <stdbool.h>

#ifdef SFXR_TESTING
bool sfxr_break_on(const char *name);
#define SFXR_BREAK(name) (sfxr_break_declared_##name && sfxr_break_on(#name))
#define SFXR_BREAK_DECLARE(name, what) enum { sfxr_break_declared_##name = 1 };
#else
#define SFXR_BREAK(name) 0
#define SFXR_BREAK_DECLARE(name, what)
#endif

// Declaring an app's switches (e.g. at the top of main.c):
//   #define BREAK SFXR_BREAK_DECLARE
//   #include "app_breaks.def"
//   #undef BREAK

#endif // SFXR_BREAK_H
