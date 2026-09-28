/* Device event frames on interrupt IN 0x82, shared by every running task.
 *
 * One asynchronous interrupt transfer is kept pending while at least one listener is registered
 * (AI, AO and counter tasks can run at the same time and all need their events). Frames are
 * dispatched to every listener; each picks the object classes it cares about.
 * Frame: u16 slot, u16 total, u16 length, u16 event id, u16 class, u16 instance, i32 status.
 *
 * Locks: events_ctl serialises add/remove (incl. opening/closing the stream); events_lock guards
 * the listener list and is held while listeners run, so after md_events_remove() returns the
 * listener is never called again. Listeners must not call md_events_add/remove. */
#include "internal.h"
#include "proto.h"

#include <string.h>

static size_t on_frame(void *user, const uint8_t *data, size_t len, size_t requested, int status)
{
    mydaq_device *dev = user;
    md_event ev;
    size_t i, next;
    (void)requested;

    memset(&ev, 0, sizeof ev);
    if (!data) {
        md_mutex_lock(&dev->events_lock);
        next = dev->nlisteners ? MD_EVENT_SIZE : 0;
        md_mutex_unlock(&dev->events_lock);
        return next;
    }
    ev.transport_status = status;
    if (status == MYDAQ_OK && len >= 16) {
        ev.id = md_get_u16(data + 6);
        ev.object_class = md_get_u16(data + 8);
        ev.instance = md_get_u16(data + 10);
        ev.status = (int32_t)md_get_u32(data + 12);
        MD_DBG(dev->ctx, "event %u on %04x/%u: status %ld", (unsigned)ev.id,
               (unsigned)ev.object_class, (unsigned)ev.instance, (long)ev.status);
    } else if (status == MYDAQ_OK) {
        return MD_EVENT_SIZE;   /* runt frame: ignore */
    } else if (status == MYDAQ_ERR_NO_DEVICE) {
        md_note_error(dev, status);
    }

    md_mutex_lock(&dev->events_lock);
    for (i = 0; i < dev->nlisteners; ++i)
        dev->listeners[i].fn(dev->listeners[i].user, &ev);
    next = dev->nlisteners && status == MYDAQ_OK ? MD_EVENT_SIZE : 0;
    md_mutex_unlock(&dev->events_lock);
    return next;
}

int md_events_add(mydaq_device *dev, md_event_fn fn, void *user)
{
    int rc = MYDAQ_OK, first;
    md_mutex_lock(&dev->events_ctl);
    md_mutex_lock(&dev->events_lock);
    if (dev->nlisteners == MD_MAX_LISTENERS) {
        md_mutex_unlock(&dev->events_lock);
        md_mutex_unlock(&dev->events_ctl);
        return MYDAQ_ERR_NO_MEM;
    }
    dev->listeners[dev->nlisteners].fn = fn;
    dev->listeners[dev->nlisteners].user = user;
    first = dev->nlisteners++ == 0;
    md_mutex_unlock(&dev->events_lock);

    if (first) {
        /* stale frames from an earlier task must not be taken for ours */
        md_drain(dev, MD_EP_EVENT_IN, MYDAQ_EP_INTERRUPT, 4);
        rc = md_pump_acquire(dev);
        if (rc == MYDAQ_OK) {
            rc = dev->ops->stream_open(dev->impl, MD_EP_EVENT_IN, MYDAQ_EP_INTERRUPT, MD_EVENT_SIZE,
                                       1, on_frame, dev, &dev->event_stream);
            if (rc != MYDAQ_OK)
                md_pump_release(dev);
        }
        if (rc != MYDAQ_OK) {
            md_mutex_lock(&dev->events_lock);
            dev->nlisteners = 0;
            md_mutex_unlock(&dev->events_lock);
        }
    }
    md_mutex_unlock(&dev->events_ctl);
    return rc;
}

void md_events_remove(mydaq_device *dev, md_event_fn fn, void *user)
{
    size_t i;
    int last = 0, found = 0;
    md_mutex_lock(&dev->events_ctl);
    md_mutex_lock(&dev->events_lock);
    for (i = 0; i < dev->nlisteners; ++i) {
        if (dev->listeners[i].fn == fn && dev->listeners[i].user == user) {
            memmove(&dev->listeners[i], &dev->listeners[i + 1],
                    (dev->nlisteners - i - 1) * sizeof dev->listeners[0]);
            --dev->nlisteners;
            found = 1;
            last = dev->nlisteners == 0;
            break;
        }
    }
    md_mutex_unlock(&dev->events_lock);
    if (found && last && dev->event_stream) {
        dev->ops->stream_close(dev->impl, dev->event_stream);
        dev->event_stream = NULL;
        md_pump_release(dev);
    }
    md_mutex_unlock(&dev->events_ctl);
}
