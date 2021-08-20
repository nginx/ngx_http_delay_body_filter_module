
/*
 * Copyright (C) Maxim Dounin
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_msec_t    delay;
} ngx_http_delay_body_conf_t;


typedef struct {
    ngx_event_t   event;
    ngx_chain_t  *out;
    ngx_uint_t    buffered;
} ngx_http_delay_body_ctx_t;


static ngx_int_t ngx_http_delay_body_filter(ngx_http_request_t *r,
    ngx_chain_t *in);
static void ngx_http_delay_body_cleanup(void *data);
static void ngx_http_delay_body_event_handler(ngx_event_t *ev);
static void *ngx_http_delay_body_create_conf(ngx_conf_t *cf);
static char *ngx_http_delay_body_merge_conf(ngx_conf_t *cf, void *parent,
    void *child);
static ngx_int_t ngx_http_delay_body_init(ngx_conf_t *cf);


static ngx_command_t  ngx_http_delay_body_commands[] = {

    { ngx_string("delay_body"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_delay_body_conf_t, delay),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_delay_body_module_ctx = {
    NULL,                          /* preconfiguration */
    ngx_http_delay_body_init,      /* postconfiguration */

    NULL,                          /* create main configuration */
    NULL,                          /* init main configuration */

    NULL,                          /* create server configuration */
    NULL,                          /* merge server configuration */

    ngx_http_delay_body_create_conf, /* create location configuration */
    ngx_http_delay_body_merge_conf   /* merge location configuration */
};


ngx_module_t  ngx_http_delay_body_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_delay_body_module_ctx, /* module context */
    ngx_http_delay_body_commands,  /* module directives */
    NGX_HTTP_MODULE,               /* module type */
    NULL,                          /* init master */
    NULL,                          /* init module */
    NULL,                          /* init process */
    NULL,                          /* init thread */
    NULL,                          /* exit thread */
    NULL,                          /* exit process */
    NULL,                          /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_request_body_filter_pt   ngx_http_next_request_body_filter;


static ngx_int_t
ngx_http_delay_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_int_t                    rc;
    ngx_chain_t                 *cl, *ln;
    ngx_http_cleanup_t          *cln;
    ngx_http_delay_body_ctx_t   *ctx;
    ngx_http_delay_body_conf_t  *conf;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_delay_body_filter_module);

    if (!conf->delay) {
        return ngx_http_next_request_body_filter(r, in);
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "delay request body filter");

    ctx = ngx_http_get_module_ctx(r, ngx_http_delay_body_filter_module);

    if (ctx == NULL) {
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_delay_body_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        ngx_http_set_ctx(r, ctx, ngx_http_delay_body_filter_module);

#if 1 /* XXX */
        r->request_body->filter_need_buffering = 1;
#endif
    }

    if (ngx_chain_add_copy(r->pool, &ctx->out, in) != NGX_OK) {
        return NGX_ERROR;
    }

    if (!ctx->event.timedout) {
        if (!ctx->event.timer_set) {

            /* cleanup to remove the timer in case of abnormal termination */

            cln = ngx_http_cleanup_add(r, 0);
            if (cln == NULL) {
                return NGX_ERROR;
            }

            cln->handler = ngx_http_delay_body_cleanup;
            cln->data = ctx;

            /* add timer */

            ctx->event.handler = ngx_http_delay_body_event_handler;
            ctx->event.data = r;
            ctx->event.log = r->connection->log;

            ngx_add_timer(&ctx->event, conf->delay);

            ctx->buffered = 1;
#if 1 /* XXX */
            r->request_body->buffered++;
#endif
        }

        return ngx_http_next_request_body_filter(r, NULL);
    }

    if (ctx->buffered) {
        ctx->buffered = 0;
#if 1 /* XXX */
        r->request_body->buffered--;
#endif
    }

    rc = ngx_http_next_request_body_filter(r, ctx->out);

    for (cl = ctx->out; cl; /* void */) {
        ln = cl;
        cl = cl->next;
        ngx_free_chain(r->pool, ln);
    }

    ctx->out = NULL;

    return rc;
}


static void
ngx_http_delay_body_cleanup(void *data)
{
    ngx_http_delay_body_ctx_t *ctx = data;

    if (ctx->event.timer_set) {
        ngx_del_timer(&ctx->event);
    }
}


static void
ngx_http_delay_body_event_handler(ngx_event_t *ev)
{
    ngx_connection_t    *c;
    ngx_http_request_t  *r;

    r = ev->data;
    c = r->connection;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "delay request body event");

    ngx_post_event(c->read, &ngx_posted_events);
}


static void *
ngx_http_delay_body_create_conf(ngx_conf_t *cf)
{
    ngx_http_delay_body_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_delay_body_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->delay = NGX_CONF_UNSET_MSEC;

    return conf;
}


static char *
ngx_http_delay_body_merge_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_delay_body_conf_t *prev = parent;
    ngx_http_delay_body_conf_t *conf = child;

    ngx_conf_merge_msec_value(conf->delay, prev->delay, 0);

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_delay_body_init(ngx_conf_t *cf)
{
    ngx_http_next_request_body_filter = ngx_http_top_request_body_filter;
    ngx_http_top_request_body_filter = ngx_http_delay_body_filter;

    return NGX_OK;
}
