/*
 * anim.c - the per-frame animate/draw handlers of the entities (game86 tbl_anim_handlers
 * DS:202E, indexed by Entity.anim; called by entities_animate_all 1000:4374).
 *
 * The handlers are not ported yet: NULL entries are skipped by the loop.
 */
#include "entity_dispatch.h"

const AnimFn entity_anim_handlers[ANIM_HANDLER_COUNT] = { 0 };
