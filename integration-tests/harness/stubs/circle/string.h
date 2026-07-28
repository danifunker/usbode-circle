//
// Host-build stub for <circle/string.h>.
//
// Circle's CString is not needed by anything the suite compiles - the sources
// that include this header do so for other declarations that come with it -
// so this is deliberately empty rather than a partial reimplementation. If a
// firmware source under test starts using CString, add it here rather than
// letting the compiler pick up a lookalike from somewhere else.
//
#ifndef _circle_string_h
#define _circle_string_h

#endif
