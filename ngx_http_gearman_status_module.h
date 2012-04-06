#define HTML_HEADER "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\n" \
    "<html><head><title>Gearman Status</title>\n" \
    "<style type='text/css'>\n" \
    "body{font-family: 'Trebuchet MS';color: #444;background: #f9f9f9;}\n" \
    "h1{background: #eee;border: 1px solid #ddd;padding: 3px;text-shadow: #ccc 1px 1px 0;color: #756857;text-transform:uppercase;}\n" \
    "h2{padding: 3px;text-shadow: #ccc 1px 1px 0;color: #ACA39C;text-transform:uppercase;border-bottom: 1px dotted #ddd;display: inline-block;}\n" \
    "hr{color: transparent;}\n" \
    "table{width: 100%%;border: 1px solid #ddd;border-spacing:0px;}\n" \
    "table th{border-bottom: 1px dotted #ddd;background: #eee;padding: 5px;font-size: 15px;text-shadow: #fff 1px 1px 0;}\n" \
    "table td{text-align: center;padding: 5px;font-size: 13px;color: #444;text-shadow: #ccc 1px 1px 0;}\n" \
    "</style></head><body>\n"

#define HTML_FOOTER "</body></html>"

#define SERVER_INFO "<h1>Gearmn Server Status for %s</h1>\n"

#define WORKERS_TABLE_HEADER "<h2>Workers</h2>\n" \
    "<table border='0'><tr><th>File Descriptor</th><th>IP Address</th><th>Client ID</th><th>Function</th></tr>"

#define STATUS_TABLE_HEADER "<h2>Status</h2>\n" \
    "<table border='0'><tr><th>Function</th><th>Total</th><th>Running</th><th>Available Workers</th></tr>"

#define CONNECTION_ERROR "Error connecting to Gearman server %s:%d."
