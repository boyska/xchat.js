#include <stdlib.h>
#include <string.h>
#include <webkit/webkit.h>
#include "webcontroller.h"
#include "WebView.h"

static GtkWidget* create_webview(void);

UIChat *uichat_new(char *from, int type) {
	WebChatController *controller = malloc(sizeof(WebChatController));
	GtkWidget *label;
	controller->view = (GtkWidget*) create_webview();
	label = from ? gtk_label_new(from) : NULL;

	printf("CHAT creato view per %s\n", from);
	if(!get_wview_container())
		return controller;
	gtk_notebook_append_page((GtkNotebook*)get_wview_container(), controller->view, label /*widget for label*/);
	gtk_notebook_set_current_page(get_wview_container(), -1);
	gtk_widget_show_all(controller->view);
	return controller;
}

void uichat_add_msg(UIChat *chat, char* from, char* msg) {
	printf("Dovrei aggiungere messaggi da %s che dice '%s' ma sono pigrissimo", from, msg);
	return;
}

void uichat_destroy(UIChat *chat) {
	printf("LOL le destroy non servono a niente, a noi la memoria ci avanza");
	return;
}

static GtkWidget* create_webview() {
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
	//g_signal_connect(G_OBJECT(webView),"navigation-requested",G_CALLBACK(click_callback),NULL);

	gtk_widget_show (webView);
	WebKitWebFrame *frame = webkit_web_view_get_main_frame(WEBKIT_WEB_VIEW(webView));
	JSGlobalContextRef context = webkit_web_frame_get_global_context(frame);
	JSObjectRef globalobj= JSContextGetGlobalObject(context);
	//incastro la webview vera in una scrolled windows cosi' ho quello che mi aspetto dallo scrolling
	GtkWidget *scrolledw = gtk_scrolled_window_new(NULL,NULL);
	g_object_set (scrolledw, "shadow-type", GTK_SHADOW_IN, NULL);

	gtk_container_set_border_width (GTK_CONTAINER(scrolledw), 10);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(scrolledw),GTK_POLICY_AUTOMATIC,GTK_POLICY_ALWAYS);
	gtk_container_add (GTK_CONTAINER(scrolledw), webView);

	return scrolledw;
}
