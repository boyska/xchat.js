#ifndef WEBVIEW_H
#define WEBVIEW_h
#include <webkit/webkit.h>
#include <JavaScriptCore/JavaScript.h>

void setup_js(JSGlobalContextRef);
void create_wview();
WebKitWebView * get_wview();
GtkWidget* get_wview_container();
WebKitWebFrame* get_wview_frame();
JSGlobalContextRef * get_wview_context();
void w_printaraw(char* s);
void w_printchanmsg(char* n, char* s,char* stamp,int action, int high);
void w_printpartjoin(char* n, char * s, char* stamp);
#endif