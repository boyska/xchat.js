#include <stdlib.h>
#include <string.h>
#include <webkit/webkit.h>
#include <JavaScriptCore/JavaScript.h>
#include "webcontroller.h"
#include "../common/textenums.h"
#include <sys/time.h>

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
		printf("notebook created. its address is %p\n", chat_notebook);
		gtk_notebook_set_show_tabs(GTK_NOTEBOOK(chat_notebook),FALSE);
	}
}

GtkWidget* get_wview_container(){
    return chat_notebook;
}

void set_tab_by_index(gint i){ /* se esiste e' tutto bono visto che se sbaglio i non succede nulla*/
    if(chat_notebook)
        gtk_notebook_set_current_page(GTK_NOTEBOOK(chat_notebook),i);
}




void create_webview(WebChatController *);

UIChat *uichat_new(char *from, int type) {
	WebChatController *controller = malloc(sizeof(WebChatController));
	GtkWidget *label;
    /* glielo passo come argomento perche' inizializza' un bel po di roba*/
    create_webview(controller);
	label = from ? gtk_label_new(from) : NULL;

	printf("CHAT creato view per %s a %p\n", from, controller);
	if(!get_wview_container())
		return controller;
	if(from){ /* XXX: l'appendo solo se from!=NULL, serve crearlo in quel caso? investigare.*/
	    controller->p_index=gtk_notebook_append_page((GtkNotebook*)get_wview_container(), controller->view, label /*widget for label*/);
	    gtk_notebook_set_current_page(GTK_NOTEBOOK(get_wview_container()), -1);
	    gtk_widget_show_all(controller->view);
	}
	return controller;
}


volatile int PRONTO = FALSE; /* globale, e volatile, speriamo basti a non rompere tutto */




const JSObjectRef uichat_invokev(UIChat *chat, char function[] , char arg_format[], ...) {



	/* Invoke a javascript function in the UIChat. The arguments type are
	 * described by the stringarg_format, as a list of types, each described by
	 * a char.
	 's'	string
	 'd'	integer
	 'f'	float/double
	 'b'	boolean
	 ' '	undefined (note that you must anyway pass a parameter for undefined,
			even if it will be ignored; NULL is a good choice)
	*/
	va_list args;
	JSValueRef *js_args;
	JSValueRef result;
	int i;

	js_args = calloc(strlen(arg_format), sizeof(JSValueRef));
	va_start(args, arg_format);
	for(i=0; arg_format[i]; i++) {
		switch(arg_format[i]) {
			case 's':
				js_args[i] = JSValueMakeString(chat->context,
						JSStringCreateWithUTF8CString(va_arg(args, char*)));
				break;
			case 'd':
				js_args[i] = JSValueMakeNumber(chat->context, (double)va_arg(args, int));
				break;
			case 'f':
				js_args[i] = JSValueMakeNumber(chat->context, (double)va_arg(args, double));
				break;
			case 'b':
				js_args[i] = JSValueMakeBoolean(chat->context, va_arg(args, int));
				break;
			case ' ':
			default:
				va_arg(args, void*);
				js_args[i] = JSValueMakeUndefined(chat->context);
				break;
		}
	}
    JSObjectRef functionObject = (JSObjectRef)JSObjectGetProperty(chat->context, chat->globalobj,JSStringCreateWithUTF8CString(function), NULL);
    result = JSObjectCallAsFunction(chat->context, functionObject, chat->globalobj, i, js_args, NULL);
	va_end(args);
	free(js_args);

	return (const JSObjectRef) result;
}

void w_printpartjoin(UIChat *chat,char* n, char * s, char* stamp){
	uichat_invokev(chat, "w_linepjoin", "sss", n, s, stamp);
}

void w_printchanmsg(UIChat *chat,char* n, char* s,char* stamp,int action, int high){
	uichat_invokev(chat, "w_linechanmsg", "sssbb", stamp ? stamp:"none", n, s, action, high);
}
void w_printprivmsg(UIChat *chat, char*n, char*s) {
	uichat_invokev(chat, "w_linechanmsg", "sssbb", "none", n, s, 0, 0);
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
        //TODO: questo si "perde" il primo messaggio di una chat privata. Forse
		//e' gestito da un'altra parte?
		printf("Priv chat at address %p\n", chat);
		w_printprivmsg(chat, from, msg);
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





static JSValueRef callbacka(JSContextRef ctx, JSObjectRef ff/*func*/, JSObjectRef obj/*self*/, size_t argc, const JSValueRef argv[], JSValueRef* e/*exception*/)
{

	PRONTO=TRUE;
	fprintf(stderr,"\n\n--------------------------\nOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO\n--------------------------\n");
	// Create the return value
	JSStringRef str = JSStringCreateWithUTF8CString("ANO");
	JSValueRef  ret = JSValueMakeString(ctx, str);
	JSStringRelease(str);
	return ret;
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



	PRONTO=FALSE; /* la setto a false perche' me la risettera' il js appena pronto
	XXX: controllare in caso di interleave (puo' succedere?) se qualcuno perde il primo
	*/


	JSStringRef fname = JSStringCreateWithUTF8CString("jsready");
	JSObjectRef func = JSObjectMakeFunctionWithCallback(controller->context, fname,callbacka);
	JSObjectSetProperty(controller->context, controller->globalobj, fname, func, kJSPropertyAttributeNone, NULL);
	JSStringRelease(fname);
	contello=0;

	while(!PRONTO){
		if(gtk_events_pending()) gtk_main_iteration(); /* mentre aspetto faccio cose, vedo gente */

	}




	gtk_container_set_border_width (GTK_CONTAINER(controller->view), 10);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(controller->view),GTK_POLICY_AUTOMATIC,GTK_POLICY_ALWAYS);
	gtk_container_add (GTK_CONTAINER(controller->view), webView);

}
