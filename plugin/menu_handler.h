#pragma once

#include <foobar2000/SDK/foobar2000.h>
#include "../include/types.h"
#include <memory>

namespace ai_metadata {

TrackInput extract_track_input(metadb_handle_ptr handle);

class AICore;

AICore* get_ai_core_instance();

bool restart_all_workers();

bool are_workers_healthy();

}
