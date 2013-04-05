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


void create_wview(){
	if(!chat_notebook) {
		chat_notebook = gtk_notebook_new();
		printf("notebook created. its address is %x\n", chat_notebook);
		/*gtk_notebook_append_page( (GtkNotebook*)chat_notebook, gtk_label_new("Pippo"), NULL);
		gtk_notebook_append_page( (GtkNotebook*)chat_notebook, gtk_label_new("Pluto"), NULL);*/
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

