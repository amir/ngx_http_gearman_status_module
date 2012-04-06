#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_gearman_status_module.h"

typedef struct {
    ngx_str_t   hostname;
    ngx_uint_t  port;
} ngx_http_gearman_status_conf_t;

enum task {
    TASK_STATUS  = 0,
    TASK_WORKERS = 1
};

static char *ngx_http_set_gearman_status(ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf);

static void *ngx_http_gearman_status_create_loc_conf(ngx_conf_t *cf);

static char *ngx_http_gearman_status_merge_loc_conf(ngx_conf_t *cf,
        void *parent, void *child);

static ngx_command_t ngx_http_gearman_status_commands[] = {
    {
        ngx_string("gearman_status"),
        NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
        ngx_http_set_gearman_status,
        0,
        0,
        NULL
    },

    {
        ngx_string("gearman_status_hostname"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_gearman_status_conf_t, hostname),
        NULL
    },

    {
        ngx_string("gearman_status_port"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_gearman_status_conf_t, port),
        NULL
    },

    ngx_null_command
};
        
static ngx_http_module_t  ngx_http_gearman_status_module_ctx = {
    NULL,                                   /* preconfiguration */
    NULL,                                   /* postconfiguration */

    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */

    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */

    ngx_http_gearman_status_create_loc_conf,/* create location configuration */
    ngx_http_gearman_status_merge_loc_conf  /* merge location configuration */
};

ngx_module_t  ngx_http_gearman_status_module = {
    NGX_MODULE_V1,
    &ngx_http_gearman_status_module_ctx,    /* module context */
    ngx_http_gearman_status_commands,       /* module directives */
    NGX_HTTP_MODULE,                        /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    NULL,                                   /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    NULL,                                   /* exit process */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};

int readline(int socket_fd, char *buffer, size_t len)
{
    char *bufx = buffer;
    static char *bp;
    static int cnt = 0;
    static char b[1500];
    char c;

    while (--len > 0) {
        if (--cnt <= 0) {
            cnt = recv(socket_fd, b, sizeof(b), 0);
            if (cnt < 0) {
                if (errno == EINTR) {
                    len++;
                    continue;
                }
                return -1;
            }
            if (cnt == 0)
                return 0;
            bp = b;
        }
        c = *bp++;
        *buffer++ = c;
        if (c == '\n') {
            *buffer = '\0';
            return buffer - bufx;
        }
    }

    return -1;
}

static inline ngx_chain_t *get_last_chain(ngx_chain_t *c)
{
    ngx_chain_t *last = c;

    if (last != NULL)
        while (last->next != NULL)
            last = last->next;

    return last;
}

static inline off_t get_content_length(ngx_chain_t *c)
{
    off_t l = 0;

    while (c != NULL) {
        l += ngx_buf_size(c->buf);
        c = c->next;
    }

    return l;
}


static ngx_chain_t *get_header(ngx_http_request_t *r)
{
    ngx_chain_t *c;
    ngx_buf_t *b;

    b = ngx_create_temp_buf(r->pool, sizeof(HTML_HEADER));
    if (b == NULL)
        return NULL;
    c = ngx_pcalloc(r->pool, sizeof(ngx_chain_t));
    if (c == NULL)
        return NULL;

    b->last = ngx_sprintf(b->last, HTML_HEADER);
    c->buf = b;
    c->next = NULL;

    return c;
}

static ngx_chain_t *
get_connection_error(ngx_http_request_t *r, const char *hostname, int port)
{
    ngx_chain_t *c;
    ngx_buf_t *b;
    size_t size;

    size = sizeof(CONNECTION_ERROR) + NGX_INT32_LEN + strlen(hostname);
    b = ngx_create_temp_buf(r->pool, size);
    if (b == NULL)
        return NULL;
    c = ngx_pcalloc(r->pool, sizeof(ngx_chain_t));
    if (c == NULL)
        return NULL;

    b->last = ngx_sprintf(b->last, CONNECTION_ERROR, hostname, port);
    c->buf = b;
    c->next = NULL;

    return c;
}

static ngx_chain_t *get_footer(ngx_http_request_t *r)
{
    ngx_chain_t *c;
    ngx_buf_t *b;

    b = ngx_create_temp_buf(r->pool, sizeof(HTML_FOOTER));
    if (b == NULL)
        return NULL;
    c = ngx_pcalloc(r->pool, sizeof(ngx_chain_t));
    if (c == NULL)
        return NULL;

    b->last = ngx_sprintf(b->last, HTML_FOOTER);
    c->buf = b;
    c->next = NULL;

    return c;
}

static ngx_chain_t *get_info(ngx_http_request_t *r, int socket_fd, int task)
{
    char line[128];
    char buffer[50];
    int i, j = 0;
    char *delim;
    char *token;
    ngx_chain_t *c;
    ngx_buf_t *b;
    size_t size;
    size_t size_per_row;

    if (task == TASK_WORKERS) {
        size = sizeof(WORKERS_TABLE_HEADER);
        size_per_row = sizeof("<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>");
        sprintf(buffer, "workers\n");
        delim = " ";
    } else {
        size = sizeof(STATUS_TABLE_HEADER);
        size_per_row = sizeof("<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>");
        sprintf(buffer, "status\n");
        delim = "\t";
    }
    write(socket_fd, buffer, strlen(buffer));
    while (readline(socket_fd, line, sizeof(line)) > 0) {
        if (line[0] == '.' && line[1] == '\n')
            break;
        for (i = 0, token = strtok(line, delim); token; token = strtok(NULL, delim), i++) {
            if (token == NULL)
                break;

            size += sizeof(token);

            if (task == TASK_WORKERS)
                if (i == 3)
                    continue;
        }
        j++;
    }
    size += size_per_row * j;
    size += sizeof("</table>");

    b = ngx_create_temp_buf(r->pool, size);
    if (b == NULL)
        return NULL;

    c = ngx_pcalloc(r->pool, sizeof(ngx_chain_t));
    if (c == NULL)
        return NULL;

    if (task == TASK_WORKERS)
        b->last = ngx_sprintf(b->last, WORKERS_TABLE_HEADER);
    else
        b->last = ngx_sprintf(b->last, STATUS_TABLE_HEADER);

    write(socket_fd, buffer, strlen(buffer));
    while (readline(socket_fd, line, sizeof(line)) > 0) {
        if (line[0] == '.' && line[1] == '\n')
            break;
        b->last = ngx_sprintf(b->last, "<tr>");
        for (i = 0, token = strtok(line, delim); token; token = strtok(NULL, delim), i++) {
            if (token == NULL)
                break;

            if (task == TASK_WORKERS && i == 3)
                continue;

            b->last = ngx_sprintf(b->last, "<td>%s</td>", token);
        }

        b->last = ngx_sprintf(b->last, "</tr>");
    }
    
    b->last = ngx_sprintf(b->last, "</table>");
    
    c->buf = b;
    c->next = NULL;

    return c;
}

static ngx_chain_t *
get_version(ngx_http_request_t *r, int socket_fd, const char *hostname)
{
    char line[10];
    char buffer[50];
    ngx_chain_t *c;
    ngx_buf_t *b;
    size_t size;

    c = ngx_pcalloc(r->pool, sizeof(ngx_chain_t));
    sprintf(buffer, "version\n");
    write(socket_fd, buffer, strlen(buffer));
    if (readline(socket_fd, line, sizeof(line)) > 0) {
        size = sizeof(SERVER_INFO) + strlen(hostname) + sizeof("Version: ") + sizeof(line) + sizeof("<hr />");
        b = ngx_create_temp_buf(r->pool, size);
        if (b == NULL)
            return NULL;
        if (c == NULL)
            return NULL;

        b->last = ngx_sprintf(b->last, SERVER_INFO, hostname);
        b->last = ngx_sprintf(b->last, "Version: %s", line);
        b->last = ngx_sprintf(b->last, "<hr />");

        c->buf = b;
        c->next = NULL;
    } else {
        c->buf = NULL;
    }

    return c;
}

static ngx_int_t ngx_http_gearman_status_handler(ngx_http_request_t *r)
{
    int         status = 1;
    int         socket_fd;
    ngx_int_t   rc;
    struct      sockaddr_in name;
    struct      hostent* hostinfo;
    ngx_chain_t *fc, *mc, *lc;
    ngx_http_gearman_status_conf_t *gscf;
    gscf = ngx_http_get_module_loc_conf(r, ngx_http_gearman_status_module);

    if (r->method != NGX_HTTP_GET)
        return NGX_HTTP_NOT_ALLOWED;

    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK)
        return rc;

    r->headers_out.content_type.len  = sizeof( "text/html; charset=ISO-8859-1" ) - 1;
    r->headers_out.content_type.data = (u_char *) "text/html; charset=ISO-8859-1";

    fc = get_header(r);
    if (fc == NULL)
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    lc = get_last_chain(fc);

    socket_fd = socket(PF_INET, SOCK_STREAM, 0);
    name.sin_family = AF_INET;
    hostinfo = gethostbyname((const char *) gscf->hostname.data);
    name.sin_addr = *((struct in_addr *) hostinfo->h_addr);
    name.sin_port = htons(gscf->port);
    if (connect(socket_fd, &name, sizeof(struct sockaddr_in)) < 0) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "Error connecting to Gearman server %s:%d",
                gscf->hostname.data, gscf->port
        );
        status = 0;
    }

    if (status) {
        mc = get_version(r, socket_fd, (const char *)gscf->hostname.data);
        if (mc == NULL)
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        lc->next = mc;
        lc = get_last_chain(mc);

        mc = get_info(r, socket_fd, TASK_STATUS);
        if (mc == NULL)
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        lc->next = mc;
        lc = get_last_chain(mc);

        mc = get_info(r, socket_fd, TASK_WORKERS);
        if (mc == NULL)
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        lc->next = mc;
        lc = get_last_chain(mc);
    } else {
        mc = get_connection_error(r, (const char *)gscf->hostname.data, gscf->port);
        if (mc == NULL)
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        lc->next = mc;
        lc = get_last_chain(mc);
    }
    shutdown(socket_fd, 2);

    mc = get_footer(r);
    if (mc == NULL)
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    lc->next = mc;
    lc = get_last_chain(mc);
    
    lc->buf->last_buf = 1;

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = get_content_length(fc);

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    return ngx_http_output_filter(r, fc);
}

static char *
ngx_http_set_gearman_status(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_gearman_status_handler;

    return NGX_CONF_OK;
}

static void *
ngx_http_gearman_status_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_gearman_status_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_gearman_status_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }
    conf->port = NGX_CONF_UNSET_UINT;

    return conf;
}

static char *
ngx_http_gearman_status_merge_loc_conf(ngx_conf_t *cf,
        void *parent, void *child)
{
    ngx_http_gearman_status_conf_t *prev = parent;
    ngx_http_gearman_status_conf_t *conf = child;

    ngx_conf_merge_str_value(conf->hostname, prev->hostname, "localhost");
    ngx_conf_merge_uint_value(conf->port, prev->port, 4730);

    return NGX_CONF_OK;
}
