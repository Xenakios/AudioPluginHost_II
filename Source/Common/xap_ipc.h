#pragma once

#include "clap/events.h"

static const char *g_pipename = "\\\\.\\pipe\\clap_pipe";

// pipe messages should start with these byte pattern,
// if they don't, something has went wrong somewhere and should abort
static const uint64_t messageMagicClap = 0xFFFFFFFF00EE0000;
static const uint64_t messageMagicCustom = 0xFFFFFFFF00EF0000;

static const uint32_t maxPipeMessageLen = 512;

struct xclap_string_event
{
    clap_event_header header;
    char str[maxPipeMessageLen - sizeof(clap_event_header) - sizeof(uint64_t)];
};
