#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkselection.h>
#include <gtk/gtkclipboard.h>
#include <gtk/gtkversion.h>
#include <gtk/gtkwindow.h>
#include <webkit/webkit.h>
#include <JavaScriptCore/JavaScript.h>
#include "WebView.h"
#include "../common/util.h"
GtkWidget* webView;
GtkWidget* scrolledw;
WebKitWebFrame* frame;
JSGlobalContextRef context;
JSObjectRef globalobj;

//XXX: definire una struttura js un po' piu' seria e cercare di fare le cose pulite

/* METODI DEFINITI NEL JS (vediamo di mantenere i nomi uguali)*/

JSStringRef clear;

void printaraw(char* s){
JSStringRef printaraw= JSStringCreateWithUTF8CString("printaraw");
JSValueRef  arguments[1];
JSValueRef result;
int num_arguments = 1;
//ci strippo via gli ansii cazzi colorazzi perche' spaccano la JSValueMakeString
JSStringRef string = JSStringCreateWithUTF8CString(strip_color(s,strlen(s), STRIP_ALL));
arguments[0] = JSValueMakeString(context,string);
JSObjectRef functionObject = (JSObjectRef)JSObjectGetProperty(context, globalobj, printaraw, NULL);
result = JSObjectCallAsFunction(context, functionObject, globalobj, num_arguments, arguments, NULL);
}




void create_wview(){
    webView = webkit_web_view_new();
    //XXX: gestire a modino tutti i path (locali?) della roba (html,js,css)
    char path[PATH_MAX];
    memset(path, 0, sizeof(path));
    if (readlink("/proc/self/exe", path, PATH_MAX-6)<=0){ //ci sottraggo 5 per '.html' e 1 per il null che non ci mette
    perror("readlink");
    exit(-1);
    }
    sprintf(path,"%s.html",path);
    webkit_web_view_open(WEBKIT_WEB_VIEW(webView), path);
    gtk_widget_show (webView);
    frame = webkit_web_view_get_main_frame(WEBKIT_WEB_VIEW(webView));
    context= webkit_web_frame_get_global_context(frame);
    globalobj= JSContextGetGlobalObject(context);
    //incastro la webview vera in una scrolled windows cosi' ho quello che mi aspetto dallo scrolling
    scrolledw = gtk_scrolled_window_new(NULL,NULL);
    gtk_container_set_border_width (GTK_CONTAINER(scrolledw), 10);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(scrolledw),GTK_POLICY_AUTOMATIC,GTK_POLICY_ALWAYS);
    gtk_container_add (GTK_CONTAINER(scrolledw), webView);
}

WebKitWebView * get_wview(){
    return WEBKIT_WEB_VIEW(webView);
}

GtkWidget* get_wview_container(){
    return scrolledw;
}

WebKitWebFrame* get_wview_frame(){
    return frame;
}
JSGlobalContextRef * get_wview_context(){
    return &context;
}

JSObjectRef * get_wview_globalobj(){
    return &globalobj;
}