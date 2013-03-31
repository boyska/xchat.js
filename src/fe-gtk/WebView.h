#ifndef WEBVIEW_H
#define WEBVIEW_h


void setup_js(JSGlobalContextRef);
void create_wview();
WebKitWebView * get_wview();
GtkWidget* get_wview_container();
WebKitWebFrame* get_wview_frame();
JSGlobalContextRef * get_wview_context();
void printaraw(char* s);
#endif