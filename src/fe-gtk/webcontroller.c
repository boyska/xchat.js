#include <stdlib.h>
#include <string.h>
#include <webkit/webkit.h>
#include <JavaScriptCore/JavaScript.h>
#include "webcontroller.h"
#include "../common/textenums.h"


GtkWidget* chat_notebook = NULL;

//TODO: definire una struttura js un po' piu' seria e cercare di fare le cose pulite
/*
void w_clear(){
    //e' l'altro metodo, e' perfetto per tutta la roba che non ritorna
    webkit_web_view_execute_script(WEBKIT_WEB_VIEW(webView),"w_clear();");
    //in teoria funziona anche la open con javascript: come url

}
*/
void create_wview(){
	if(!chat_notebook) {
		chat_notebook = gtk_notebook_new();
		printf("notebook created. its address is %x\n", chat_notebook);
		gtk_notebook_set_show_tabs(chat_notebook,FALSE);
	}
}

GtkWidget* get_wview_container(){
    return chat_notebook;
}

void set_tab_by_index(gint i){ /* se esiste e' tutto bono visto che se sbaglio i non succede nulla*/
    if(chat_notebook) 
        gtk_notebook_set_current_page(chat_notebook,i);
}




void create_webview(WebChatController *);

UIChat *uichat_new(char *from, int type) {
	WebChatController *controller = malloc(sizeof(WebChatController));
	GtkWidget *label;
    /* glielo passo come argomento perche' inizializza' un bel po di roba*/	
    create_webview(controller);
	label = from ? gtk_label_new(from) : NULL;

	printf("CHAT creato view per %s\n", from);
	if(!get_wview_container())
		return controller;
	if(from){ /* XXX: l'appendo solo se from!=NULL, serve crearlo in quel caso? investigare.*/
	    controller->p_index=gtk_notebook_append_page((GtkNotebook*)get_wview_container(), controller->view, label /*widget for label*/);
	    gtk_notebook_set_current_page(get_wview_container(), -1);
	    gtk_widget_show_all(controller->view);
	}
	return controller;
}



char* palette[6]={"#E01B6A", "#7E1BE0","#1B7EE0","#1BE070","#D6E01B", "#E05D1B" };
int nickbycolor(char* n){ /*XXX: la roba piu' scema del mondo, inserire i colori personalizzati, hash a modo del nick etc etc*/
    int i = 0, sum = 0;
    while (n[i])
      sum += n[i++];
   return sum%6;
}



void w_printpartjoin(UIChat *chat,char* n, char * s, char* stamp){
    JSStringRef chanmsg= JSStringCreateWithUTF8CString("w_linepjoin");
    JSValueRef  arguments[3];
    JSValueRef result;
    int num_arguments = 3;
    JSStringRef string = JSStringCreateWithUTF8CString((const char *)s);
    JSStringRef nick = JSStringCreateWithUTF8CString((const char *)n);
    JSStringRef timero = JSStringCreateWithUTF8CString(stamp?stamp:"none");
    arguments[0] = JSValueMakeString(chat->context,nick);
    arguments[1] = JSValueMakeString(chat->context,string);
    arguments[2] = JSValueMakeString(chat->context,timero);
    JSObjectRef functionObject = (JSObjectRef)JSObjectGetProperty(chat->context, chat->globalobj,chanmsg, NULL);
    result = JSObjectCallAsFunction(chat->context, functionObject, chat->globalobj, num_arguments, arguments, NULL);
}

void w_printchanmsg(UIChat *chat,char* n, char* s,char* stamp,int action, int high){
    JSStringRef chanmsg= JSStringCreateWithUTF8CString("w_linechanmsg");
    JSValueRef  arguments[6];
    JSValueRef result;
    int num_arguments = 6;
    JSStringRef string = JSStringCreateWithUTF8CString((const char *)s);
    JSStringRef nick = JSStringCreateWithUTF8CString((const char *)n);
    int colnum=nickbycolor(n);
    char* colorozzo=palette[colnum];
    JSStringRef color = JSStringCreateWithUTF8CString((const char *)colorozzo);
    JSStringRef timero = JSStringCreateWithUTF8CString(stamp?stamp:"none");
    arguments[0] = JSValueMakeString(chat->context,timero);
    arguments[1] = JSValueMakeString(chat->context,color);
    arguments[2] = JSValueMakeString(chat->context,nick);
    arguments[3] = JSValueMakeString(chat->context,string);
    arguments[4] = JSValueMakeBoolean(chat->context,action);
    arguments[5] = JSValueMakeBoolean(chat->context,high);

    JSObjectRef functionObject = (JSObjectRef)JSObjectGetProperty(chat->context, chat->globalobj,chanmsg, NULL);
    result = JSObjectCallAsFunction(chat->context, functionObject, chat->globalobj, num_arguments, arguments, NULL);

}




void uichat_add_msg(UIChat *chat, char* from, char* msg,int index,char* stamp) {

	switch (index)
	{
	case XP_TE_JOIN:
	case XP_TE_PART:
	case XP_TE_PARTREASON:
	case XP_TE_QUIT:
		w_printpartjoin(chat,from,msg,stamp);
		break;

	/* ===Private message=== */
	case XP_TE_PRIVMSG:
	case XP_TE_DPRIVMSG:
	case XP_TE_PRIVACTION:
	case XP_TE_DPRIVACTION:
        //TODO: da capire se serve una nuova chat, se e' già fatta etc. (all'interno basta usare channel message poi)
        2+2;
		break;

	/* ===Highlighted message=== */
	case XP_TE_HCHANACTION:
	case XP_TE_HCHANMSG:
	    w_printchanmsg(chat,from,msg,stamp,index==XP_TE_HCHANACTION,true);
		break;

	/* ===Channel message=== */
	case XP_TE_CHANACTION:
	case XP_TE_CHANMSG:
		w_printchanmsg(chat,from,msg,stamp,index==XP_TE_CHANACTION,false);
		break;
	}

}

void uichat_destroy(UIChat *chat) {
	printf("LOL le destroy non servono a niente, a noi la memoria ci avanza");
	return;
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



void create_webview(WebChatController* controller) {
	GtkWidget *webView = webkit_web_view_new();
	//XXX: gestire a modino tutti i path (locali?) della roba (html,js,css)
	char path[PATH_MAX];
	memset(path, 0, sizeof(path));
	if (readlink("/proc/self/exe", path, PATH_MAX-6)<=0){ //ci sottraggo 5 per '.html' e 1 per il null che non ci mette
		perror("readlink");
		exit(-1);
	}
	sprintf(path,"%s.html",path);
	webkit_web_view_open(WEBKIT_WEB_VIEW(webView), path);
	g_signal_connect(G_OBJECT(webView),"navigation-requested",G_CALLBACK(click_callback),NULL);

	gtk_widget_show (webView);
	controller->frame = webkit_web_view_get_main_frame(WEBKIT_WEB_VIEW(webView));
	controller->context = webkit_web_frame_get_global_context(controller->frame);
	controller->globalobj= JSContextGetGlobalObject(controller->context);
	//incastro la webview vera in una scrolled windows cosi' ho quello che mi aspetto dallo scrolling
	controller->view = gtk_scrolled_window_new(NULL,NULL);
	g_object_set (controller->view, "shadow-type", GTK_SHADOW_IN, NULL);

	gtk_container_set_border_width (GTK_CONTAINER(controller->view), 10);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(controller->view),GTK_POLICY_AUTOMATIC,GTK_POLICY_ALWAYS);
	gtk_container_add (GTK_CONTAINER(controller->view), webView);

}
