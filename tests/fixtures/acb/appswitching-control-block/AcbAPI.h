// SPDX-License-Identifier: GPL-3.0-or-later
// Test double for the SDK declarations, not a runtime library or installed header.
// Production compiles against the actual NDK AcbAPI.h.
// Reference: webosbrew/webos-userland @ 00a5f8721e15d5c10030b99b2f3e73e0c527bcce
#pragma once
extern "C" {
using AcbAPI_Callback = void (*)(long, long, long, long, long, const char *);
enum AcbAPI_PlayerType { PLAYER_TYPE_MSE = 10 };
enum AcbAPI_SinkType { SINK_TYPE_MAIN = 0 };
enum AcbAPI_AppState { APPSTATE_FOREGROUND = 1 };
enum AcbAPI_PlayState { PLAYSTATE_UNLOADED = 0, PLAYSTATE_LOADED = 1, PLAYSTATE_PLAYING = 2, PLAYSTATE_PAUSED = 3 };
long AcbAPI_create();
bool AcbAPI_initialize(long, AcbAPI_PlayerType, const char *, AcbAPI_Callback);
void AcbAPI_destroy(long);
int AcbAPI_finalize(long);
bool AcbAPI_setSinkType(long, AcbAPI_SinkType);
bool AcbAPI_setMediaId(long, const char *);
int AcbAPI_setMediaVideoData(long, const char *, long *);
int AcbAPI_setMediaAudioData(long, const char *, long *);
int AcbAPI_setState(long, AcbAPI_AppState, AcbAPI_PlayState, long *);
int AcbAPI_setDisplayWindow(long, long, long, long, long, bool, long *);
int AcbAPI_setCustomDisplayWindow(long, long, long, long, long, long, long, long, long, bool, long *);
}
