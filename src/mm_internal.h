/* Helpers shared by mm_agent.c and the state machine (mm_fsm.c). */
#ifndef MM_INTERNAL_H
#define MM_INTERNAL_H

#include "mm_agent.h"

/* mm_link_t.phase */
enum { MM_LINK_CLOSED = 0, MM_LINK_CONNECTING, MM_LINK_HANDSHAKE, MM_LINK_UP, MM_LINK_FAILED };

/* Link id for mm_tcp_init (selects the W5500 socket) */
enum { MM_LINK_REQ = 0, MM_LINK_PUB = 1, MM_LINK_SUB = 2 };

void mm_link_open(micromads_agent_t *a, mm_link_t *l, uint16_t port);
void mm_link_close(mm_link_t *l);

/* Sends REQ requests: ["", version, "settings", name] / ["", version, "timecode"] */
bool mm_request_settings(micromads_agent_t *a);
bool mm_request_timecode(micromads_agent_t *a);

/* Uses the settings / timecode reply. Return false if it is unusable. */
bool mm_apply_settings(micromads_agent_t *a);
bool mm_apply_timecode(micromads_agent_t *a);

/* Subscribes to every sub topic on the SUB link. */
bool mm_send_subscriptions(micromads_agent_t *a);

/* Publishes the agent_event "startup" message. */
void mm_publish_startup(micromads_agent_t *a);

/* True if the agent subscribes to anything (a SUB link is needed). */
bool mm_needs_sub(const micromads_agent_t *a);

/* Milliseconds since `since`. */
static inline uint32_t mm_elapsed(uint32_t since) { return mm_millis() - since; }

#endif /* MM_INTERNAL_H */
