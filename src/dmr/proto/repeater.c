#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "dmr/bits.h"
#include "dmr/error.h"
#include "dmr/log.h"
#include "dmr/proto/repeater.h"
#include "dmr/payload/emb.h"
#include "dmr/payload/lc.h"
#include "dmr/payload/sync.h"
#include "dmr/time.h"

const char *dmr_repeater_proto_name = "dmrlib repeater";

static int repeater_proto_init(void *repeaterptr)
{
    dmr_log_debug("repeater: init %p", repeaterptr);

    dmr_repeater_t *repeater = (dmr_repeater_t *)repeaterptr;
    if (repeater == NULL) {
        return dmr_error(DMR_EINVAL);
    }

    if (repeater->slots < 2) {
        dmr_log_error("repeater: can't start with less than 2 slots, got %d",
            repeater->slots);
        return dmr_error(DMR_EINVAL);
    }
    if (repeater->color_code < 1 || repeater->color_code > 15) {
        dmr_log_error("repeater: can't start, invalid color code %d",
            repeater->color_code);
        return dmr_error(DMR_EINVAL);
    }

    return 0;
}

static int repeater_proto_start_thread(void *repeaterptr)
{
    dmr_log_debug("repeater: start thread %d", dmr_thread_id(NULL) % 1000);
    dmr_repeater_t *repeater = (dmr_repeater_t *)repeaterptr;
    if (repeater == NULL)
        return dmr_thread_error;

    dmr_thread_name_set("repeater");
    dmr_log_mutex("repeater: mutex lock");
    dmr_mutex_lock(repeater->proto.mutex);
    repeater->proto.is_active = true;
    dmr_log_mutex("repeater: mutex unlock");
    dmr_mutex_unlock(repeater->proto.mutex);
    dmr_repeater_loop(repeater);
    dmr_thread_exit(dmr_thread_success);
    return dmr_thread_success;
}

static int repeater_proto_start(void *repeaterptr)
{
    dmr_log_debug("repeater: start %p", repeaterptr);
    dmr_repeater_t *repeater = (dmr_repeater_t *)repeaterptr;

    if (repeater == NULL)
        return dmr_error(DMR_EINVAL);

    if (repeater->proto.thread != NULL) {
        dmr_log_error("repeater: can't start, already active");
        return dmr_error(DMR_EINVAL);
    }

    dmr_mutex_lock(repeater->proto.mutex);
    repeater->proto.is_active = false;
    dmr_mutex_unlock(repeater->proto.mutex);
    repeater->proto.thread = talloc_zero(repeater, dmr_thread_t);
    if (repeater->proto.thread == NULL) {
        return dmr_error(DMR_ENOMEM);
    }

    if (dmr_thread_create(repeater->proto.thread, repeater_proto_start_thread, repeater) != dmr_thread_success) {
        dmr_log_error("repeater: can't create thread");
        return dmr_error(DMR_EINVAL);
    }
    return 0;
}

static bool repeater_proto_active(void *repeaterptr)
{
    dmr_log_mutex("repeater: active");
    dmr_repeater_t *repeater = (dmr_repeater_t *)repeaterptr;
    if (repeater == NULL)
        return false;

    dmr_log_mutex("repeater: mutex lock");
    dmr_mutex_lock(repeater->proto.mutex);
    bool active = repeater->proto.thread != NULL && repeater->proto.is_active;
    dmr_log_mutex("repeater: mutex unlock");
    dmr_mutex_unlock(repeater->proto.mutex);
    dmr_log_mutex("repeater: active = %s", DMR_LOG_BOOL(active));
    return active;
}

static int repeater_proto_stop(void *repeaterptr)
{
    dmr_log_debug("repeater: stop %p", repeaterptr);
    dmr_repeater_t *repeater = (dmr_repeater_t *)repeaterptr;
    if (repeater == NULL)
        return dmr_error(DMR_EINVAL);

    if (repeater->proto.thread == NULL) {
        fprintf(stderr, "repeater: not active\n");
        return 0;
    }

    dmr_mutex_lock(repeater->proto.mutex);
    repeater->proto.is_active = false;
    dmr_mutex_unlock(repeater->proto.mutex);
    if (dmr_thread_join(*repeater->proto.thread, NULL) != dmr_thread_success) {
        dmr_log_error("repeater: can't join thread");
        return dmr_error(DMR_EINVAL);
    }

    dmr_thread_exit(dmr_thread_success);
    TALLOC_FREE(repeater->proto.thread);
    repeater->proto.thread = NULL;
    return 0;
}

static int repeater_proto_wait(void *repeaterptr)
{
    dmr_log_trace("repeater: wait %p", repeaterptr);
    dmr_repeater_t *repeater = (dmr_repeater_t *)repeaterptr;
    if (repeater == NULL)
        return dmr_error(DMR_EINVAL);

    if (!repeater_proto_active(repeater))
        return 0;

    int ret;
    if ((ret = dmr_thread_join(*repeater->proto.thread, NULL)) != 0) {
        dmr_log_error("repeater: failed to join thread: %s", strerror(errno));
    }
    return 0;
}

static void repeater_proto_rx_cb(dmr_proto_t *proto, void *userdata, dmr_packet_t *packet_in)
{
    dmr_log_trace("repeater: rx callback %p", userdata);
    dmr_repeater_t *repeater = (dmr_repeater_t *)userdata;
    if (proto == NULL || repeater == NULL || packet_in == NULL)
        return;

    if (dmr_repeater_queue(repeater, proto, packet_in) != 0) {
        dmr_log_error("repeater: failed to add packet to queue");
        return;
    }
}

dmr_repeater_t *dmr_repeater_new(dmr_repeater_route_t route)
{
    dmr_repeater_t *repeater = talloc_zero(NULL, dmr_repeater_t); // malloc(sizeof(dmr_repeater_t));
    if (repeater == NULL)
        return NULL;
    //memset(repeater, 0, sizeof(dmr_repeater_t));
    repeater->color_code = 1;
    repeater->route = route;
    if (repeater->route == NULL) {
        dmr_log_warn("repeater: got a NULL router, hope that's okay...");
    }

    // Setup timeslots
    repeater->ts = talloc_size(repeater, sizeof(dmr_repeater_timeslot_t) * 2);
    if (repeater->ts == NULL) {
        dmr_log_critical("repeater: out of memory");
        TALLOC_FREE(repeater);
        return NULL;
    }

    dmr_ts_t ts;
    for (ts = DMR_TS1; ts < DMR_TS_INVALID; ts++) {
        if ((repeater->ts[ts].lock = talloc_zero(repeater, dmr_mutex_t)) == NULL) {
            dmr_log_critical("repeater[%u]: out of memory", ts);
            TALLOC_FREE(repeater);
            return NULL;
        }
        if ((repeater->ts[ts].last_voice_frame_received = talloc_zero(repeater, struct timeval)) == NULL) {
            dmr_log_critical("repeater[%u]: out of memory", ts);
            TALLOC_FREE(repeater);
            return NULL;
        }
        if ((repeater->ts[ts].last_data_frame_received = talloc_zero(repeater, struct timeval)) == NULL) {
            dmr_log_critical("repeater[%u]: out of memory", ts);
            TALLOC_FREE(repeater);
            return NULL;
        }
        repeater->ts[ts].stream_id = 1;
        repeater->ts[ts].sequence = 0;
        repeater->ts[ts].voice_call_active = false;
        repeater->ts[ts].data_call_active = false;
    }

    // Setup receiving queue
    repeater->queue_size = 32;
    repeater->queue_used = 0;
    repeater->queue_lock = talloc_zero(repeater, dmr_mutex_t);
    repeater->queue = talloc_size(repeater, sizeof(dmr_repeater_item_t) * repeater->queue_size);

    if (repeater->queue == NULL) {
        dmr_log_critical("repeater: out of memory");
        TALLOC_FREE(repeater);
        return NULL;
    }

    // Setup repeater protocol
    repeater->proto.name = dmr_repeater_proto_name;
    repeater->proto.type = DMR_PROTO_REPEATER;
    repeater->proto.init = repeater_proto_init;
    repeater->proto.start = repeater_proto_start;
    repeater->proto.stop = repeater_proto_stop;
    repeater->proto.wait = repeater_proto_wait;
    repeater->proto.active = repeater_proto_active;
    repeater->slots = 0;
    if (dmr_proto_mutex_init(&repeater->proto) != 0) {
        dmr_log_error("repeater: failed to init mutex");
        free(repeater);
        return NULL;
    }

    return repeater;
}

/** Add a packet coming from proto the the processing queue.
 * This function allocates for the item, as well as copies for the proto and
 * packet
 */
int dmr_repeater_queue(dmr_repeater_t *repeater, dmr_proto_t *proto, dmr_packet_t *packet)
{
    if (repeater == NULL || proto == NULL || packet == NULL)
        return dmr_error(DMR_EINVAL);

    //dmr_mutex_lock(repeater->queue_lock);
    int ret = 0;
    if (repeater->queue_used + 1 < repeater->queue_size) {
        dmr_repeater_item_t *item = talloc_zero(NULL, dmr_repeater_item_t);
        if (item == NULL) {
            dmr_log_critical("repeater: out of memory");
            return dmr_error(DMR_ENOMEM);
        }
        // Link the proto instance, but create a copy of the packet so the
        // caller can free()/recycle it after passing it to us.
        item->proto = proto;
        item->packet = talloc_zero(item, dmr_packet_t);
        if (item->packet == NULL) {
            dmr_log_critical("repeater: out of memory");
            return dmr_error(DMR_ENOMEM);
        }
        memcpy(item->packet, packet, sizeof(dmr_packet_t));
        repeater->queue[repeater->queue_used++] = item;
    } else {
        dmr_log_error("repeater: queue full!");
        ret = -1;
    }
    //dmr_mutex_unlock(repeater->queue_lock);

    return ret;
}

dmr_repeater_item_t *dmr_repeater_queue_shift(dmr_repeater_t *repeater)
{
    if (repeater == NULL)
        return NULL;

    dmr_repeater_item_t *item = NULL;
    //dmr_mutex_lock(repeater->queue_lock);
    if (repeater->queue_used) {
        item = repeater->queue[0];
        repeater->queue_used--;
        size_t i;
        for (i = 1; i < repeater->queue_used; i++) {
            repeater->queue[i - 1] = repeater->queue[i];
        }
    }
   // dmr_mutex_unlock(repeater->queue_lock);

    return item;
}

static bool dmr_repeater_voice_call_active(dmr_repeater_t *repeater, dmr_ts_t ts)
{
    bool active;
    dmr_mutex_lock(repeater->ts[ts].lock);
    active = repeater->ts[ts].voice_call_active;
    dmr_mutex_unlock(repeater->ts[ts].lock);
    return active;
}

static void dmr_repeater_voice_call_set_active(dmr_repeater_t *repeater, dmr_ts_t ts, bool active)
{
    dmr_mutex_lock(repeater->ts[ts].lock);
    repeater->ts[ts].voice_call_active = active;
    dmr_mutex_unlock(repeater->ts[ts].lock);
}

static int dmr_repeater_voice_call_end(dmr_repeater_t *repeater, dmr_packet_t *packet)
{
    if (repeater == NULL || packet == NULL)
        return dmr_error(DMR_EINVAL);

    dmr_ts_t ts = packet->ts;
    dmr_repeater_timeslot_t rts = repeater->ts[ts];

    if (!dmr_repeater_voice_call_active(repeater, ts)) {
        dmr_log_debug("repeater[%u]: not stopping inactive voice call", ts);
        return 0;
    }

    dmr_log_trace("repeater: voice call end on %s", dmr_ts_name(ts));
    if (rts.vbptc_emb_lc != NULL) {
        dmr_vbptc_16_11_free(rts.vbptc_emb_lc);
    }
    dmr_repeater_voice_call_set_active(repeater, ts, false);
    return 0;
}

static int dmr_repeater_voice_call_start(dmr_repeater_t *repeater, dmr_packet_t *packet, dmr_full_lc_t *full_lc)
{
    if (repeater == NULL || packet == NULL) {
        dmr_log_error("repeater: can't start voice call, received NULL");
        return dmr_error(DMR_EINVAL);
    }

    dmr_ts_t ts = packet->ts;
    dmr_repeater_timeslot_t rts = repeater->ts[ts];

    if (dmr_repeater_voice_call_active(repeater, ts)) {
        dmr_log_debug("repeater[%u]: terminating ready-active voice call", ts);
        return dmr_repeater_voice_call_end(repeater, packet);
    }

    dmr_log_info("repeater[%u]: voice call start", ts);
    dmr_repeater_voice_call_set_active(repeater, ts, true);

    rts.voice_frame = 0;

    if (full_lc != NULL) {
        dmr_log_trace("repeater: constructing emb LC");
        if ((rts.vbptc_emb_lc = dmr_vbptc_16_11_new(8, NULL)) == NULL) {
            return dmr_error(DMR_ENOMEM);
        }

        dmr_emb_signalling_lc_bits_t *ebits = talloc(NULL, dmr_emb_signalling_lc_bits_t);
        dmr_emb_signalling_lc_bits_t *ibits;
        if (ebits == NULL)
            return dmr_error(DMR_ENOMEM);

        if (dmr_emb_encode_signalling_lc_from_full_lc(full_lc, ebits, packet->data_type) != 0) {
            TALLOC_FREE(ebits);
            return dmr_error(DMR_LASTERROR);
        }
        if ((ibits = dmr_emb_signalling_lc_interlave(ebits)) == NULL) {
            TALLOC_FREE(ebits);
            return dmr_error(DMR_ENOMEM);
        }
        if (dmr_vbptc_16_11_encode(rts.vbptc_emb_lc, ibits->bits, sizeof(dmr_emb_signalling_lc_bits_t)) != 0) {
            TALLOC_FREE(ebits);
            return dmr_error(DMR_LASTERROR);
        }
        TALLOC_FREE(ebits);
    }

    return 0;
}

static void dmr_repeater_expire(dmr_repeater_t *repeater)
{
    dmr_ts_t ts;
    for (ts = DMR_TS1; ts < DMR_TS_INVALID; ts++) {
        dmr_repeater_timeslot_t rts = repeater->ts[ts];
        if (dmr_repeater_voice_call_active(repeater, ts)) {
            uint32_t delta = dmr_time_ms_since(*rts.last_voice_frame_received);
            //dmr_log_debug("repeater[%u]: active, delta=%lums (expire)", ts, delta);
            if (delta > 180) {
                dmr_log_info("repeater[%u]: voice call expired after %lums", ts, delta);
                dmr_packet_t *packet = talloc_zero(NULL, dmr_packet_t);
                packet->ts = ts;
                packet->data_type = DMR_DATA_TYPE_TERMINATOR_WITH_LC;
                dmr_repeater_voice_call_end(repeater, packet);
            }
        }
    }
}

void dmr_repeater_loop(dmr_repeater_t *repeater)
{
    // Run callbacks
    while (repeater_proto_active(repeater)) {
        dmr_repeater_expire(repeater);
        dmr_repeater_item_t *item = dmr_repeater_queue_shift(repeater);
        if (item != NULL) {
            // Just for convenience.
            dmr_proto_t *proto = item->proto;
            dmr_packet_t *packet_in = item->packet;

            dmr_log_debug("repeater: handle packet from %s", proto->name);
            if (repeater->slots == 0) {
                dmr_log_error("repeater: no slots!?");
                goto next;
            }

            size_t i = 0;
            for (; i < repeater->slots; i++) {
                dmr_repeater_slot_t *slot = &repeater->slot[i];
                if (slot->proto == proto) {
                    dmr_log_trace("repeater: skipped same-proto %s", slot->proto->name);
                    continue;
                }
                dmr_log_debug("repeater: call route callback %p for %s->%s",
                    repeater->route, proto->name, slot->proto->name);

                // We work with another copy of the packet, because the
                // router may alter the packet and we want to give each
                // router a copy of the original packet.
                dmr_packet_t *packet = talloc(NULL, dmr_packet_t);
                if (packet == NULL) {
                    dmr_log_error("repeater: no memory to clone packet!");
                    return;
                }
                memcpy(packet, packet_in, sizeof(dmr_packet_t));

                dmr_route_t policy = DMR_ROUTE_PERMIT;
                if (repeater->route != NULL && (policy = repeater->route(repeater, proto, slot->proto, packet)) == DMR_ROUTE_REJECT) {
                    dmr_log_debug("repeater: dropping packet, refused by router");
                    /* Clean up our *cloned* packet. */
                    TALLOC_FREE(packet);
                    continue;
                }

                dmr_log_debug("repeater: routing %s packet from %s->%s",
                    dmr_data_type_name(packet->data_type),
                    proto->name, slot->proto->name);

                if (policy == DMR_ROUTE_PERMIT) {
                    dmr_ts_t ts = packet->ts;
                    dmr_repeater_timeslot_t rts = repeater->ts[ts];

                    switch (packet->data_type) {
                    case DMR_DATA_TYPE_VOICE_SYNC:
                    case DMR_DATA_TYPE_VOICE:
                        /* Book keeping */
                        gettimeofday(rts.last_voice_frame_received, NULL);

                        /* Handle late entry. */
                        if (!dmr_repeater_voice_call_active(repeater, ts)) {
                            dmr_log_trace("repeater[%u]: no voice call active, starting", ts);
                            if (dmr_repeater_voice_call_start(repeater, packet, NULL) != 0) {
                                dmr_log_error("repeater[%u]: failed to start voice call: %s", ts, dmr_error_get());
                                continue;
                            }

                            // Prepend voice LC headers, we've apparently missed one before.
                            dmr_log_debug("repeater[%u]: prepend voice LC", ts);
                            dmr_packet_t *header = talloc_zero(packet, dmr_packet_t);
                            if (header == NULL) {
                                dmr_log_error("repeater[%u]: could not prepend header, out of memory", ts);
                                TALLOC_FREE(packet);
                                return;
                            }
                            uint8_t i;
                            for (i = 0; i < 4; i++) {
                                memcpy(header, packet, sizeof(dmr_packet_t));
                                header->data_type = DMR_DATA_TYPE_VOICE_LC;
                                dmr_repeater_fix_headers(repeater, header);
                                dmr_proto_tx(slot->proto, slot->userdata, header);
                            }
                            TALLOC_FREE(header);
                        }

                        // Update voice frame
                        packet->meta.voice_frame = (rts.voice_frame++) % 6;
                        break;

                    case DMR_DATA_TYPE_VOICE_LC:
                        /* Book keeping */
                        gettimeofday(rts.last_voice_frame_received, NULL);

                        if (dmr_repeater_voice_call_start(repeater, packet, NULL) != 0) {
                            dmr_log_error("repeater[%u]: failed to start voice call: %s", ts, dmr_error_get());
                            continue;
                        }
                        break;

                    case DMR_DATA_TYPE_TERMINATOR_WITH_LC:
                        /* Book keeping */
                        gettimeofday(rts.last_voice_frame_received, NULL);

                        if (dmr_repeater_voice_call_end(repeater, packet) != 0) {
                            dmr_log_error("repeater[%u]: failed to end voice call: %s", ts, dmr_error_get());
                            continue;
                        }
                        break;                            

                    default:
                        break;
                    }

                    dmr_repeater_fix_headers(repeater, packet);
                }
                dmr_proto_tx(slot->proto, slot->userdata, packet);
           
                /* Clean up our *cloned* packet. */
                TALLOC_FREE(packet);
            }
        }

        // If we reach here, we want to return to processing the queue ASAP
        //TALLOC_FREE(item);
        continue;

next:
        // If we reach here, there was a serious error, so we back off a bit
        if (item != NULL) {
            TALLOC_FREE(item);
            item = NULL;
        }
        dmr_msleep(5);
    }
}

int dmr_repeater_add(dmr_repeater_t *repeater, void *userdata, dmr_proto_t *proto)
{
    dmr_log_trace("repeater: add(%p, %p, %p)", repeater, userdata, proto);
    if (repeater == NULL || userdata == NULL || proto == NULL) {
        dmr_log_error("repeater: add received NULL pointer");
        return dmr_error(DMR_EINVAL);
    }

    if (repeater->slots >= DMR_REPEATER_MAX_SLOTS) {
        dmr_log_error("repeater: max slots of %d reached", DMR_REPEATER_MAX_SLOTS);
        return dmr_error(DMR_EINVAL);
    }

    // Register a calback for the repeater on the protocol.
    if (!dmr_proto_rx_cb_add(proto, repeater_proto_rx_cb, repeater)) {
        dmr_log_error("repeater: protocol %s callback refused", proto->name);
        return dmr_error(DMR_EINVAL);
    }

    repeater->slot[repeater->slots].userdata = userdata;
    repeater->slot[repeater->slots].proto = proto;
    repeater->slots++;
    dmr_log_info("repeater: added protocol %s", proto->name);
    return 0;
}

int dmr_repeater_fix_headers(dmr_repeater_t *repeater, dmr_packet_t *packet)
{
    dmr_ts_t ts = packet->ts;
    dmr_repeater_timeslot_t rts = repeater->ts[ts];
    dmr_log_trace("repeater[%u]: fixed headers in %s packet",
        ts, dmr_data_type_name(packet->data_type));

    if (repeater == NULL || packet == NULL)
        return dmr_error(DMR_EINVAL);

    if (packet->color_code != repeater->color_code) {
        dmr_log_debug("repeater[%u]: setting color code %u->%u",
            ts, packet->color_code, repeater->color_code);
        packet->color_code = repeater->color_code;
    }

    switch (packet->data_type) {
    case DMR_DATA_TYPE_VOICE_LC:
        dmr_log_trace("repeater[%u]: constructing Full Link Control", ts);

        // Regenerate full LC
        dmr_full_lc_t *full_lc = talloc_zero(NULL, dmr_full_lc_t);
        if (full_lc == NULL) {
             dmr_log_error("repeater[%u]: out of memory", ts);
             return -1;
        }
        full_lc->flco_pdu = (packet->flco == DMR_FLCO_PRIVATE) ? DMR_FLCO_PDU_PRIVATE : DMR_FLCO_PDU_GROUP;
        full_lc->fid = 0;
        full_lc->pf = 0; // (packet->flco == DMR_FLCO_PRIVATE);
        full_lc->src_id = packet->src_id;
        full_lc->dst_id = packet->dst_id;

        if (dmr_full_lc_encode(full_lc, packet) != 0) {
            dmr_log_error("repeater[%u]: can't fix headers, full LC failed: ", ts, dmr_error_get());
            return -1;
        }

        dmr_log_trace("repeater[%u]: constructing sync pattern for voice LC", ts);
        dmr_sync_pattern_encode(DMR_SYNC_PATTERN_MS_SOURCED_DATA, packet);
        dmr_log_trace("repeater[%u]: setting slot type to voice LC", ts);
        dmr_slot_type_encode(packet);
        break;

    case DMR_DATA_TYPE_TERMINATOR_WITH_LC:
        dmr_sync_pattern_encode(DMR_SYNC_PATTERN_MS_SOURCED_DATA, packet);
        if (dmr_repeater_voice_call_end(repeater, packet) != 0) {
            dmr_log_error("repeater[%u]: failed to end voice call: %s", ts, dmr_error_get());
            return -1;
        }
        break;

    case DMR_DATA_TYPE_VOICE_SYNC:
    case DMR_DATA_TYPE_VOICE:
        dmr_log_trace("repeater[%u]: setting SYNC data", ts);
        dmr_emb_signalling_lc_bits_t emb_bits;
        memset(&emb_bits, 0, sizeof(emb_bits));
        int ret = 0;

        dmr_emb_t emb = {
            .color_code = repeater->color_code,
            .pi         = packet->flco == DMR_FLCO_PRIVATE,
            .lcss       = DMR_EMB_LCSS_SINGLE_FRAGMENT
        };

        switch (packet->meta.voice_frame + 'A') {
        case 'A':
            dmr_log_trace("repeater[%u]: constructing sync pattern for voice SYNC in frame A", ts);
            ret = dmr_sync_pattern_encode(DMR_SYNC_PATTERN_MS_SOURCED_VOICE, packet);
            break;

        case 'B':
            if (rts.vbptc_emb_lc == NULL) {
                dmr_log_trace("repeater[%u]: inserting NULL EMB LCSS fragment in frame B", ts);
            } else {
                dmr_log_trace("repeater[%u]: inserting first LCSS fragment in frame B", ts);
                emb.lcss = DMR_EMB_LCSS_FIRST_FRAGMENT;
            }
            ret = dmr_emb_lcss_fragment_encode(&emb, rts.vbptc_emb_lc, 0, packet);
            break;

        case 'C':
            if (rts.vbptc_emb_lc == NULL) {
                dmr_log_trace("repeater[%u]: inserting NULL EMB LCSS fragment in frame C", ts);
            } else {
                dmr_log_trace("repeater[%u: inserting continuation LCSS fragment in frame C", ts);
                emb.lcss = DMR_EMB_LCSS_CONTINUATION;
            }
            ret = dmr_emb_lcss_fragment_encode(&emb, rts.vbptc_emb_lc, 1, packet);
            break;

        case 'D':
            if (rts.vbptc_emb_lc == NULL) {
                dmr_log_trace("repeater[%u]: inserting NULL EMB LCSS fragment in frame D", ts);
            } else {
                dmr_log_trace("repeater[%u]: inserting continuation LCSS fragment in frame D", ts);
                emb.lcss = DMR_EMB_LCSS_CONTINUATION;
            }
            ret = dmr_emb_lcss_fragment_encode(&emb, rts.vbptc_emb_lc, 2, packet);
            break;

        case 'E':
            if (rts.vbptc_emb_lc == NULL) {
                dmr_log_trace("repeater[%u]: inserting NULL EMB LCSS fragment in frame E", ts);
            } else {
                dmr_log_trace("repeater[%u]: inserting last LCSS fragment in frame E", ts);
                emb.lcss = DMR_EMB_LCSS_LAST_FRAGMENT;
            }
            ret = dmr_emb_lcss_fragment_encode(&emb, rts.vbptc_emb_lc, 3, packet);
            break;

        case 'F':
            dmr_log_trace("repeater[%u]: inserting NULL EMB LCSS fragment in frame F", ts);
            ret = dmr_emb_lcss_fragment_encode(&emb, NULL, 0, packet);
            break;

        default:
            break;
        }

        if (ret != 0) {
            dmr_log_error("repeater[%u]: error setting SYNC data: %s", ts, dmr_error_get());
            return ret;
        }

        break;

    default:
        dmr_log_trace("repeater[%u]: not altering %s packet", ts, dmr_data_type_name(packet->data_type));
        break;
    }

    return 0;
}

void dmr_repeater_free(dmr_repeater_t *repeater)
{
    if (repeater == NULL)
        return;

    TALLOC_FREE(repeater);
}
