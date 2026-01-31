#pragma once

#include <stdbool.h>
#include <stddef.h>

# define TB_REMOTING_PROTOCOL_VERSION_STRING "v1"

bool tb_discovery_start(int port, char* err, size_t errcap);
void tb_discovery_stop(void);
