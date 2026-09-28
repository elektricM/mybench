/* Context lifetime and logging. */
#include "internal.h"

#include <libusb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int context_alloc(mydaq_context **out)
{
    mydaq_context *ctx;
    if (!out)
        return MYDAQ_ERR_INVALID_ARG;
    *out = NULL;
    ctx = calloc(1, sizeof *ctx);
    if (!ctx)
        return MYDAQ_ERR_NO_MEM;
    if (md_mutex_init(&ctx->log_lock) != 0) {
        free(ctx);
        return MYDAQ_ERR_NO_MEM;
    }
    ctx->log_level = MYDAQ_LOG_WARNING;
    *out = ctx;
    return MYDAQ_OK;
}

int mydaq_context_new(mydaq_context **out)
{
    mydaq_context *ctx;
    int rc = context_alloc(&ctx);
    if (rc != MYDAQ_OK)
        return rc;
    rc = libusb_init(&ctx->usb);
    if (rc != LIBUSB_SUCCESS) {
        md_mutex_destroy(&ctx->log_lock);
        free(ctx);
        return md_usb_error(rc);
    }
    ctx->owns_usb = 1;
    *out = ctx;
    return MYDAQ_OK;
}

int mydaq_context_new_with_libusb(struct libusb_context *usb, mydaq_context **out)
{
    mydaq_context *ctx;
    int rc;
    if (!usb)
        return MYDAQ_ERR_INVALID_ARG;
    rc = context_alloc(&ctx);
    if (rc != MYDAQ_OK)
        return rc;
    ctx->usb = usb;
    *out = ctx;
    return MYDAQ_OK;
}

int mydaq_context_new_no_usb(mydaq_context **out) { return context_alloc(out); }

void mydaq_context_free(mydaq_context *ctx)
{
    if (!ctx)
        return;
    if (ctx->owns_usb && ctx->usb)
        libusb_exit(ctx->usb);
    md_mutex_destroy(&ctx->log_lock);
    free(ctx);
}

void mydaq_context_set_log(mydaq_context *ctx, mydaq_log_level level, mydaq_log_fn fn, void *user)
{
    if (!ctx)
        return;
    md_mutex_lock(&ctx->log_lock);
    ctx->log_level = level;
    ctx->log_fn = fn;
    ctx->log_user = user;
    md_mutex_unlock(&ctx->log_lock);
}

int md_log_enabled(mydaq_context *ctx, mydaq_log_level level)
{
    int enabled;
    if (!ctx)
        return 0;
    md_mutex_lock(&ctx->log_lock);
    enabled = level != MYDAQ_LOG_NONE && level <= ctx->log_level;
    md_mutex_unlock(&ctx->log_lock);
    return enabled;
}

void md_log(mydaq_context *ctx, mydaq_log_level level, const char *fmt, ...)
{
    static const char *const names[] = {"", "error", "warning", "info", "debug", "trace"};
    char msg[2048];
    mydaq_log_fn fn;
    void *user;
    va_list ap;

    if (!ctx)
        return;
    md_mutex_lock(&ctx->log_lock);
    if (level == MYDAQ_LOG_NONE || level > ctx->log_level) {
        md_mutex_unlock(&ctx->log_lock);
        return;
    }
    fn = ctx->log_fn;
    user = ctx->log_user;
    md_mutex_unlock(&ctx->log_lock);

    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (fn)
        fn(user, level, msg);
    else
        fprintf(stderr, "libmydaq %s: %s\n", names[level <= MYDAQ_LOG_TRACE ? level : MYDAQ_LOG_TRACE],
                msg);
}
