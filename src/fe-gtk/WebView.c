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
#include "WebView.h"
#include "../common/util.h"
int CREATED=0;
GtkWidget* webView;
GtkWidget* chat_notebook = NULL;
GtkWidget* scrolledw;
WebKitWebFrame* frame;
JSGlobalContextRef context=NULL;
JSObjectRef globalobj;

enum line_type {
    text,
    backlog,
    servermsg,
    privatemsg,
    highlight
  };
typedef enum line_type linetype;
//XXX: definire una struttura js un po' piu' seria e cercare di fare le cose pulite

/* METODI DEFINITI NEL JS (vediamo di mantenere i nomi uguali)*/

void w_clear(){
    //e' l'altro metodo, e' perfetto per tutta la roba che non ritorna
    webkit_web_view_execute_script(WEBKIT_WEB_VIEW(webView),"w_clear();");
    //in teoria funziona anche la open con javascript: come url

}

void w_printaraw(char* s){
    if(context==NULL) create_wview();
    JSStringRef printaraw= JSStringCreateWithUTF8CString("w_lineraw");
    JSValueRef  arguments[1];
    JSValueRef result;
    int num_arguments = 1;
    //ci strippo via gli ansii cazzi colorazzi perche' spaccano la JSValueMakeString
    s=strip_color(s,-1, STRIP_ALL);
    JSStringRef string = JSStringCreateWithUTF8CString((const char *)s);
    arguments[0] = JSValueMakeString(context,string);
    JSObjectRef functionObject = (JSObjectRef)JSObjectGetProperty(context, globalobj, printaraw, NULL);
    result = JSObjectCallAsFunction(context, functionObject, globalobj, num_arguments, arguments, NULL);

}


/*
void w_printline(linetype type, char* text, char* nickname){


}
*/


char* palette[6]={"#E01B6A", "#7E1BE0","#1B7EE0","#1BE070","#D6E01B", "#E05D1B" };
int nickbycolor(char* n){ /*XXX: la roba piu' scema del mondo, inserire i colori personalizzati, hash a modo del nick etc etc*/
    int i = 0, sum = 0;
    while (n[i])
      sum += n[i++];
   return sum%6;
}


void w_printpartjoin(char* n, char * s, char* stamp){
    if(context==NULL) create_wview();
    JSStringRef chanmsg= JSStringCreateWithUTF8CString("w_linepjoin");
    JSValueRef  arguments[3];
    JSValueRef result;
    int num_arguments = 3;
    JSStringRef string = JSStringCreateWithUTF8CString((const char *)s);
    JSStringRef nick = JSStringCreateWithUTF8CString((const char *)n);
    JSStringRef timero = JSStringCreateWithUTF8CString(stamp?stamp:"none");
    arguments[0] = JSValueMakeString(context,nick);
    arguments[1] = JSValueMakeString(context,string);
    arguments[2] = JSValueMakeString(context,timero);
    JSObjectRef functionObject = (JSObjectRef)JSObjectGetProperty(context, globalobj,chanmsg, NULL);
    result = JSObjectCallAsFunction(context, functionObject, globalobj, num_arguments, arguments, NULL);
}

//function(t,c,n,s)
void w_printchanmsg(char* n, char* s,char* stamp,int action, int high){
    if(context==NULL) create_wview();
    JSStringRef chanmsg= JSStringCreateWithUTF8CString("w_linechanmsg");
    JSValueRef  arguments[6];
    JSValueRef result;
    int num_arguments = 6;
    //ci strippo via gli ansii cazzi colorazzi perche' spaccano la JSValueMakeString
    JSStringRef string = JSStringCreateWithUTF8CString((const char *)s);
    JSStringRef nick = JSStringCreateWithUTF8CString((const char *)n);
    int colnum=nickbycolor(n);
    char* colorozzo=palette[colnum];
    JSStringRef color = JSStringCreateWithUTF8CString((const char *)colorozzo);
    JSStringRef timero = JSStringCreateWithUTF8CString(stamp?stamp:"none");
    arguments[0] = JSValueMakeString(context,timero);
    arguments[1] = JSValueMakeString(context,color);
    arguments[2] = JSValueMakeString(context,nick);
    arguments[3] = JSValueMakeString(context,string);
    arguments[4] = JSValueMakeBoolean(context,action);
    arguments[5] = JSValueMakeBoolean(context,high);

    JSObjectRef functionObject = (JSObjectRef)JSObjectGetProperty(context, globalobj,chanmsg, NULL);
    result = JSObjectCallAsFunction(context, functionObject, globalobj, num_arguments, arguments, NULL);

}

/*
XXX: se l'uri e' del tipo file:/// per adesso uso la policy di default
da riguardare.
Negli altri casi apro l'uri nel browser con lo stesso metodo di xchat originale.
Inoltre maschero il click per evitare che ci vada anche la webview
*/
WebKitNavigationResponse click_callback(WebKitWebView* web_view,WebKitWebFrame* frame,WebKitNetworkRequest *request,gpointer user_data){
    const char *new_uri =   webkit_network_request_get_uri(request);
    int result=strncmp(new_uri,"file",4);
    if(result!=0){
        fe_open_url(new_uri);
        return WEBKIT_NAVIGATION_RESPONSE_IGNORE;
    }
    else
        return WEBKIT_NAVIGATION_RESPONSE_ACCEPT;
}





void create_wview(){
	if(!chat_notebook) {
		chat_notebook = gtk_notebook_new();
		printf("notebook created. its address is %x\n", chat_notebook);
		gtk_notebook_append_page( (GtkNotebook*)chat_notebook, gtk_label_new("Pippo"), NULL);
		gtk_notebook_append_page( (GtkNotebook*)chat_notebook, gtk_label_new("Pluto"), NULL);
	}
}

WebKitWebView * get_wview(){
    return WEBKIT_WEB_VIEW(webView);
}

GtkWidget* get_wview_container(){
    return chat_notebook;
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

