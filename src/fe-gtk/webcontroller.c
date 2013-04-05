#include "webcontroller.h"

UIChat *uichat_new(char *from, int type) {
	WebChatController *controller = malloc(sizeof(WebChatController));
	controller->view = (GtkWidget*) webkit_web_view_new();

	printf("CHAT creato view per %s", from);
	gtk_container_add(get_wview_container(), create_webview(), from, -1);
	return controller;
}

void uichat_add_msg(char* from, char* msg) {
	return;
}

void uichat_destroy(UIChat *chat) {
	return;
}

static GtkWidget* create_webview() {
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
	//g_signal_connect(G_OBJECT(webView),"navigation-requested",G_CALLBACK(click_callback),NULL);

	gtk_widget_show (webView);
	frame = webkit_web_view_get_main_frame(WEBKIT_WEB_VIEW(webView));
	context= webkit_web_frame_get_global_context(frame);
	globalobj= JSContextGetGlobalObject(context);
	//incastro la webview vera in una scrolled windows cosi' ho quello che mi aspetto dallo scrolling
	scrolledw = gtk_scrolled_window_new(NULL,NULL);
	g_object_set (scrolledw, "shadow-type", GTK_SHADOW_IN, NULL);

	gtk_container_set_border_width (GTK_CONTAINER(scrolledw), 10);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(scrolledw),GTK_POLICY_AUTOMATIC,GTK_POLICY_ALWAYS);
	gtk_container_add (GTK_CONTAINER(scrolledw), webView);

	return scrolledw;
}
